#pragma once
#include "YArcCacheNode.h"
#include<unordered_map>
#include<map>
#include<mutex>

namespace YCache
{
    
    //ArcLfuPart = LFU缓存分区
    //核心作用 存储热点高频访问数据，淘汰访问次数最少的缓存项
    //设计结构：
    //哈希表(快速查找) + 频率映射表（按访问次数分组）+ 双向链表（维护顺序）+幽灵缓存（ARC自适应）

    template<typename Key,typename Value>
    class ArcLfuPart
    {
        public:
        using NodeType = ArcNode<Key,Value>;
        using NodePtr = std::shared_ptr<NodeType>;
        using NodeMap = std::unordered_map<Key,NodePtr>;

        // 定义频率映射：key=访问次数，value=对应次数的节点链表
        // 有序map保证最小频率可以快速获取
        using FreqMap = std::map<size_t,std::list<NodePtr>>;

        explicit ArcLfuPart(size_t capacity,size_t transformThreshold)
        :capacity_(capacity)
        ,ghostCapacity_(capacity)
        // 初始化列表：赋值LRU升级LFU的访问次数阈值
        ,transformThreshold_(transformThreshold)
        ,minFreq_(0)
        {
            //调用私有方法 初始化幽灵缓存的双向链表
            initializeLists();
        }

        //公有方法：向LFU缓存写入数据
        bool put(Key key,Value value)
        {
            if(capacity_ == 0)
            {
                return false;
            }
            std::lock_guard<std::mutex>lock(mutex_);
            
            auto it = mainCache_.find(key);
            
            if(it!=mainCache_.end())
            {
                //更新已存在节点的value和访问频率
                return updateExistingNode(it->second,value);
            }
            return addNewNode(key,value);
        }


        bool get(Key key,Value& value)
        {
            std::lock_guard<std::mutex>lock(mutex_);
            auto it = mainCache_.find(key);
            if(it != mainCache_.end())
            {
                //LFU核心：访问后节点频率+1，更新频率分组
                updateNodeFrequency(it->second);
                value = it ->second->getValue();
                return true;

            }
            return false;
        }

        //判断key是否在主缓存中
        bool contain(Key key)
        {
            return mainCache_.find(key)!=mainCache_.end();
        }

        //检查key是否在幽灵缓存中
        //作用：ARC自适应调整容量的核心判断
        bool checkGhost(Key key)
        {
            auto it = ghostCache_.find(key);
            if(it != ghostCache_.end())
            {
                //从幽灵链表中移除节点
                removeFromGhost(it->second);
                ghostCache_.erase(it);
                return true;
            }
            return false;
        }

        void increaseCapacity()
        {
            ++capacity_;
        }
        
        //减少LFU分区容量
        bool decreaseCapacity()
        {
            if(capacity_<=0)
            {
                //容量不能为负数
                return false;
            }
            if(mainCache_.size()== capacity_  )
            {
                //只有当缓存已满时才需要淘汰
                evictLeastFrequent();
            }
            --capacity_;
            return true;
        }


        private:
        
        //初始化幽灵缓存双向链表
        void initializeLists()
        {
            ghostTail_ = std::make_shared<NodeType>();
            ghostHead_ = std::make_shared<NodeType>();
            ghostHead_->next_ = ghostTail_;
            ghostTail_->prev_ = ghostHead_;
        }

        //更新已存在节点
        bool updateExistingNode(NodePtr node,const Value& value)
        {
            node->setValue(value);
            //更新访问频率
            updateNodeFrequency(node);
            return true;
        }


        //添加新节点到LFU
        bool addNewNode(const Key& key,const Value&value)
        {
            if(mainCache_.size()>=capacity_)
            {
                evictLeastFrequent();
            }
            
            //创建新节点
            NodePtr newNode = std::make_shared<NodeType>(key,value);
            
            //加入主缓存哈希表
            mainCache_[key] = newNode;
            //加入频率映射表（首次访问频率为1）
            freqMap_[1].push_back(newNode);
            minFreq_ = 1;

            return true;

        }

        //更新节点访问频率
        void updateNodeFrequency(NodePtr node)
        {
            size_t oldFreq = node->getAccessCount();
            node->incrementAccessCount();
            size_t newFreq = node->getAccessCount();

            auto& oldList = freqMap_[oldFreq];
            oldList.remove(node);

            if(oldList.empty())
            {
                freqMap_.erase(oldFreq);
                if(oldFreq==minFreq_)
                {
                    minFreq_ = newFreq;
                } 
            }
            freqMap_[newFreq].push_back(node);
        }

        //淘汰访问频率最小的节点
        void evictLeastFrequent()
        {
            if(freqMap_.empty())
            {
                return;
            }
            //获取最小频率链表
            auto& minFreqList = freqMap_[minFreq_];
            if(minFreqList.empty())
            {
                return;
            }
            
            NodePtr leastNode = minFreqList.front();

            minFreqList.pop_front();

            if(minFreqList.empty())
            {
                //删除空频率
                freqMap_.erase(minFreq_);
                if(!freqMap_.empty())
                {
                    //更新最小频率
                    minFreq_=freqMap_.begin()->first;
                }
            }

            if(ghostCache_.size()>=ghostCapacity_)
            {
                //幽灵缓存满了
                removeOldestGhost();
            }
            
            //被淘汰节点移除节点
            addToGhost(leastNode);

            //从主缓存删除
            mainCache_.erase(leastNode->getKey());

                
        }

        //私有方法 从幽灵链表移除节点
        void removeFromGhost(NodePtr node)
        {
            if(!node->prev_.expired()&&node->next_)
            {
                auto prev = node->prev_.lock();
                prev->next_ = node->next_;
                node->next_->prev_=node->prev_;
                node->next_=nullptr;
            }
        }

        //添加节点到幽灵链表尾部
        void addToGhost(NodePtr node)
        {
            node->next_ = ghostTail_;
            node->prev_ = ghostTail_->prev_;
            if(!ghostTail_->prev_.expired())
            {
                ghostTail_->prev_.lock()->next_=node;
            }
            ghostTail_->prev_ = node;
            ghostCache_[node->getKey()]=node;
        }


        //删除幽灵链表中最老的节点（头部）
        void removeOldestGhost()
        {
            NodePtr oldestGhost = ghostHead_->next_;
            if(oldestGhost != ghostTail_)
            {
                removeFromGhost(oldestGhost);
                ghostCache_.erase(oldestGhost->getKey());
            }
        }











        private:

        size_t capacity_;
        size_t ghostCapacity_;
        size_t transformThreshold_;
        size_t minFreq_;
        std::mutex mutex_;
        NodeMap mainCache_;
        NodeMap ghostCache_;
        FreqMap freqMap_;
        NodePtr ghostHead_;
        NodePtr ghostTail_;
        

    };
}