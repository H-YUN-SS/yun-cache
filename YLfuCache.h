#pragma once
#include <unordered_map>
#include <list>
#include <mutex>
#include <memory>
#include <thread>
#include <vector>
#include <cmath>
#include <limits>
#include "YICachePolicy.h"
namespace YCache
{
    template<typename Key,typename Value>class YLfuCache;

    //1.FreqList类
    //频率双向链表
    //存储 访问频率相同的 所有缓存节点

    template<typename Key,typename Value>
    class FreqList
    {
        private:
        //内部节点结构体 存储一条完整的缓存数据
        struct Node
        {
            //节点访问频次
            int freq;
            Key key;
            Value value;
            std::weak_ptr<Node>pre;
            std::shared_ptr<Node>next;

            //默认构造函数 初始化频率为1
            Node():freq(1),next(nullptr){}

            //带参构造函数：创建带key、value的节点 频率默认为1
            Node(Key key,Value value):freq(1),key(key),value(value),next(nullptr){}
        };
        using NodePtr = std::shared_ptr<Node>;
        //当前链表对应的访问频率值
        int freq_;
        NodePtr head_;
        NodePtr tail_;

        public:

        explicit FreqList(int n):freq_(n)
        {
            head_=std::make_shared<Node>();
            tail_=std::make_shared<Node>();
            head_->next = tail_;
            tail_->pre = head_;
        }

        //判断当前频率链表是否为空 头指尾 说明无数据
        bool isEmpty() const
        {
            return head_->next == tail_;
        }

        //添加节点到链表尾部（最新位置）
        void addNode(NodePtr node)
        {
            //空指针安全判断
            if(!node||!head_||!tail_)
            {
                return;
            }


            node->pre = tail_->pre;
            node->next = tail_;
            tail_->pre.lock()->next = node;
            tail_->pre=node;
        }

        //从链表删除该节点
        void removeNode(NodePtr node)
        {
            if(!node||!head_||!tail_)
            {
                return;
            }

            if(node->pre.expired()||!node->next)
            {
                return;
            }

            auto pre = node->pre.lock();
            pre->next = node ->next;
            node->next->pre = pre;
            node->next = nullptr;
        }

        // 获取链表第一个有效节点（最久未使用+频率最低，优先淘汰）
        NodePtr getFirstNode() const
        {
            return head_->next;
        }

        friend class YLfuCache<Key,Value>;
    };


    //YLfuCache 核心LFU缓存实现
    //继承同意缓存接口，实现put/get/淘汰/频率衰减
    template<typename Key,typename Value>
    class YLfuCache : public YICachePolicy<Key,Value>
    {
        public:
        using Node = typename FreqList<Key,Value>::Node;
        using NodePtr = std::shared_ptr<Node>;
        using NodeMap = std::unordered_map<Key,NodePtr>;

        YLfuCache(int capacity,int maxAverageNum = 1000000):capacity_(capacity),minFreq_(std::numeric_limits<int>::max()),maxAverageNum_(maxAverageNum),curAverageNum_(0),curTotalNum_(0)
        {}
        ~YLfuCache() override
        {
            for(auto& pair : freqToFreqList_)
            {
                delete pair.second;
            }
        }

        //对外接口 存入缓存
        void put(Key key,Value value)override
        {
            //容量为0 无法存储
            if(capacity_ == 0)
            {
                return;
            }

            //加锁 保证多线程安全 同一时间仅一个线程修改
            std::lock_guard<std::mutex>lock(mutex_);
            auto it =nodeMap_.find(key);
            if(it!= nodeMap_.end())
            {
                // key已存在：更新节点value
                it->second->value = value;
                // 调用内部方法更新频率
                getInternal(it->second, value);
                return;
            }
            //key不存在 新增缓存逻辑
            putInternal(key,value);
        }

        //对外接口： 获取缓存（输出参数 返回是否命中）
        bool get(Key key,Value& value)override
        {
            std::lock_guard<std::mutex>lock(mutex_);

            //哈希表查找key
            auto it = nodeMap_.find(key);
            if(it!= nodeMap_.end())
            {
                //命中缓存：更新频率 返回数据
                getInternal(it->second,value);
                return true;
            }
            return false;

        }
        //对外接口 简化版get 直接返回value
        Value get(Key key)override
        {
            Value value;
            //调用带输出参数的get
            get(key,value);
            return value;
        }

        //清空所有缓存数据
        void purge()
        {
            //清空key-node哈希表
            nodeMap_.clear();
            //清空频率-链表哈希表
            freqToFreqList_.clear();
        }
        
        private:
        //内部新增缓存
        void putInternal(Key key,Value);
        //内部获取并更新频率
        void getInternal(NodePtr node,Value& value);
        //淘汰最少访问节点
        void kickOut();
        //从频率链表删除节点
        void removeFromFreqList(NodePtr node);
        //添加到频率链表
        void addToFreqList(NodePtr node);
        //更新访问统计
        void addFreqNum();
        //减少访问统计
        void decreaseFreqNum(int num);
        //频率衰减逻辑
        void handleOverMaxAverageNum();
        //更新最小访问频率
        void updateMinFreq();

        private:
        //缓存最大容量
        int capacity_;
        //当前最小访问频率
        int minFreq_;
        //最大平均访问频率阈值
        int maxAverageNum_;
        //当前平均访问频率
        int curAverageNum_;
        //所有节点总访问次数
        int curTotalNum_;
        //互斥锁
        std::mutex mutex_;
        //key->节点映射表
        NodeMap nodeMap_;
        //频率->频率链表映射
        std::unordered_map<int,FreqList<Key,Value>*>freqToFreqList_;
    };


    template<typename Key,typename Value>
    void YLfuCache<Key,Value>::getInternal(NodePtr node,Value& value)
    {
        //将要获取的Value赋值给输出参数
        value = node->value;

        //把节点从旧的频率链表中移除
        removeFromFreqList(node);

        //节点访问频率+1
        node->freq++;

        //将节点添加到新频率对应的链表
        addToFreqList(node);

        //如果节点旧频率 == 最小频率 且旧频率链表为空
        //说明最小频率需要更新+1
        if(node->freq - 1 == minFreq_ && freqToFreqList_[minFreq_]->isEmpty())
        {
            minFreq_++;
        }

        //更新总访问次数和平均访问频率
        addFreqNum();
        
    };

    //内部put实现 新增缓存节点
    template<typename Key,typename Value>
    void YLfuCache<Key,Value>::putInternal(Key key,Value value)
    {
        //如果缓存已满，执行淘汰策略
        if(nodeMap_.size() == capacity_)
        {
            //淘汰访问频率最低的节点
            kickOut();
        }
        //创建新节点
        NodePtr node =std::make_shared<Node>(key,value);
        //将新节点存入哈希表
        nodeMap_[key] = node;

        //将新节点加入频率 = 1的链表
        addToFreqList(node);

        //更新访问统计
        addFreqNum();
        
        //新节点频率=1 最小频率一定为1
        minFreq_ = 1;

    }

    //淘汰策略 删除最小频率链表的第一个节点
    template<typename Key,typename Value>
    void YLfuCache<Key,Value>::kickOut()
    {
        //获取最小频率链表的第一个节点
        NodePtr node = freqToFreqList_[minFreq_]->getFirstNode();
        //从频率链表删除该节点
        removeFromFreqList(node);
        //从哈希表删除该key
        nodeMap_.erase(node->key);
        //更新访问统计
        decreaseFreqNum(node->freq);
    }


    //从频率链表中删除节点
    template<typename Key,typename Value>
    void YLfuCache<Key,Value>::removeFromFreqList(NodePtr node)
    {
        //空节点直接返回
        if(!node)
        {
            return;
        }

        //获取节点当前频率
        auto freq = node->freq;
        //调用freqlist的删除方法
        freqToFreqList_[freq]->removeNode(node);
    }

    //将节点添加到对应频率的链表
    template<typename Key,typename Value>
    void YLfuCache<Key,Value>::addToFreqList(NodePtr node)
    {
        //空节点直接返回
        if(!node)
        {
            return;
        }
            //获取节点频率
            auto freq = node -> freq;
            
            //如果该频率链表不存在，则新建一个
            if(freqToFreqList_.find(node->freq) == freqToFreqList_.end())
            {
                freqToFreqList_[node->freq] = new FreqList<Key,Value>(node->freq);
            }

            //将新节点添加到对应频率链表
            freqToFreqList_[freq]->addNode(node);
        
    }

    //更新总访问次数&平均访问频率
    template<typename Key,typename Value>
    void YLfuCache<Key,Value>::addFreqNum()
    {
        //总访问次数+1
        curTotalNum_++;
        //缓存为空 平均频率=0
        if(nodeMap_.empty())
        {
            curAverageNum_ = 0;

        }
        else 
        {
            curAverageNum_ = curTotalNum_ / nodeMap_.size();

            //如果平均频率超过阈值 执行全局衰减
            if(curAverageNum_ > maxAverageNum_)
            {
                handleOverMaxAverageNum();
            }
        }
    }

    //淘汰节点时减少访问统计
    template<typename Key,typename Value>
    void YLfuCache<Key,Value>::decreaseFreqNum(int num)
    {
        //总访问次数减去被淘汰节点的频率
        curTotalNum_ -= num;
        //缓存为空
        if(nodeMap_.empty())
        {
            curAverageNum_ = 0;
        }
        else
        {
            curAverageNum_ = curTotalNum_ / nodeMap_.size();
        }
    }

    //全局频率衰减：防止老热点数据永久占用缓存
    template<typename Key,typename Value>
    void YLfuCache<Key,Value>::handleOverMaxAverageNum()
    {
        //空缓存直接返回
        if(nodeMap_.empty())
        {
            return;
        }
        
        //遍历所有缓存节点
        for(auto it = nodeMap_.begin();it != nodeMap_.end();it++)
        {
            //空节点跳过
            if(!it->second)
            {
                continue;
            }
            NodePtr node = it->second;

            //先从旧频率链表中删除
            removeFromFreqList(node);

            //记录旧频率
            int oldFreq = node -> freq;
            //衰减量 = 最大平均频率/2
            int decay = maxAverageNum_ / 2;
            //节点频率减去衰减量
            node->freq -= decay;

            //频率最低不能小于1
            if(node->freq <1)
            {
                node->freq=1;
            }

            //计算频率变化之 更新总统计
            int delta = node->freq - oldFreq;
            curTotalNum_ += delta;

            //将节点加入新频率链表
            addToFreqList(node);
        }
        //衰减完成后重新计算最小频率
        updateMinFreq();
    }

    //遍历所有频率链表，更新最小频率
    template<typename Key,typename Value>
    void YLfuCache<Key,Value>::updateMinFreq()
    {
        //初始化最小值为int最大值
        minFreq_ = std::numeric_limits<int>::max();
        //遍历所有频率链表
        for(const auto& pair : freqToFreqList_)
        {
            //如果链表非空，更新最小频率
            if(pair.second&&!pair.second->isEmpty())
            {
                minFreq_=std::min(minFreq_,pair.first);
            }
        }
        //如果所有链表都为空 最小频率设为1
        if(minFreq_ == std::numeric_limits<int>::max())
        {
            minFreq_ = 1;
        }
    }


    //YHashLfuCache 分片LFU 高并发优化
    //作用 大缓存切分多片，每篇独立锁，降低锁冲突
    template<typename Key,typename Value>
    class YHashLfuCache
    {
        public:
        //构造函数
        //capacity 总容量
        //sliceNum 分片数量
        //maxAverageNum:衰减阈值
        YHashLfuCache(size_t capacity, int sliceNum, int maxAverageNum = 10)
        //分片数 用户传入>0则用 否则用CPU核心数
        :sliceNum_(sliceNum>0?sliceNum:std::thread::hardware_concurrency())
        ,capacity_(capacity)
        {
            //计算每个分片的容量：向上取整
            size_t sliceSize = std::ceil(capacity_ / static_cast<double>(sliceNum_));
            //创建指定数量的LFU缓存分片
            for(int i=0;i<sliceNum_;i++)
            {
                lfuSliceCaches_.emplace_back(new YLfuCache<Key,Value>(sliceSize,maxAverageNum));
            }
        }

        //存入缓存：哈希路由到对应分片
        void put(Key key,Value value)
        {
            //计算key哈希值，取模得到分片索引
            size_t sliceIndex = Hash(key)%sliceNum_;
            //对应分片执行put
            lfuSliceCaches_[sliceIndex]->put(key,value);
        }

        //获取缓存：哈希路由
        bool get(Key key,Value&value)
        {
            //计算key哈希值 取模得到分片索引
            size_t sliceIndex = Hash(key)%sliceNum_;
            return lfuSliceCaches_[sliceIndex]->get(key,value);
        }

        //简化get
        Value get(Key key)
        {
            Value value;
            get(key,value);
            return value;
        }

        //清空所有分片缓存
        void purge()
        {
            //遍历所有分片，执行清空
            for(auto& lfuSliceCache:lfuSliceCaches_)
            {
                lfuSliceCache->purge();
            }
        }

        private:
        size_t Hash(Key key)
        {
            std::hash<Key>hashFunc;
            return hashFunc(key);
        }

        private:
        size_t capacity_;//总容量
        int sliceNum_;//分片数量
        std::vector<std::unique_ptr<YLfuCache<Key,Value>>>lfuSliceCaches_;//分片数组
    };

}



