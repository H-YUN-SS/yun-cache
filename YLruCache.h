    #pragma once

    #include<cstring>
    #include<list>
    #include<memory>
    #include<mutex>
    #include<unordered_map>
    #include <vector>      
    #include <thread>  

    #include"YICachePolicy.h"
    namespace YCache
    {
        template<typename Key, typename Value> class YLruCache;

        template<typename Key,typename Value>
        class LruNode
        {
        private:
            Key key_;
            Value value_;
            size_t accessCount_;
            std::weak_ptr<LruNode> prev_;//上一个节点的指针 弱指针
            std::shared_ptr<LruNode> next_;
        public:
            LruNode(Key key, Value value)
                : key_(key)
                , value_(value)
                , accessCount_(1)
            {}
            Key getKey()const{return key_;}
            Value getValue()const{return value_;}
            void setValue(const Value& value){value_=value;}
            size_t getAccessCount() const { return accessCount_; } 
            void incrementAccessCount() { ++accessCount_; } 

            friend class YLruCache<Key, Value>;
        };

        template<typename Key, typename Value>
        class YLruCache : public YICachePolicy<Key,Value>
        {
        public:
            //给 LruNode<Key, Value> 起一个简短别名：LruNodeType
            using LruNodeType = LruNode<Key, Value>;
            //NodePtr = 智能指针，用来安全管理节点内存，不崩溃
            using NodePtr = std::shared_ptr<LruNodeType>;
            //NodeMap = 哈希表key → 快速找到对应的节点
            using NodeMap = std::unordered_map<Key, NodePtr>;
            
            explicit YLruCache(int capacity)
                : capacity_(capacity)
            {
                initializeList();
            }

            ~YLruCache()override=default;
            
            void put(Key key, Value value) override
            {
                if (capacity_ <= 0)
                    return;
            
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = nodeMap_.find(key);
                if (it != nodeMap_.end())
                {
                    // 如果在当前容器中,则更新value,并调用get方法，代表该数据刚被访问
                    updateExistingNode(it->second, value);
                    return ;
                }

                addNewNode(key, value);
            }
            
            //对外接口 get获取缓存 带输出和验证
            bool get(Key key, Value& value) override
            {
                //加锁
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = nodeMap_.find(key);
                if(it != nodeMap_.end())
                {
                    //命中缓存 移到最新位置
                    moveToMostRecent(it->second);
                    value = it ->second->getValue();
                    return true;
                }
                return false;
            }

            //对外接口 get简化 直接返回value
            Value get(Key key) override
            {
                Value value{};
                get(key,value);
                return value;
            }
            
            //对外接口 删除缓存
            //根据key删除缓存
            void remove(Key key)
            {
                //加锁
                std::lock_guard<std::mutex>lock(mutex_);
                //查找key
                auto it = nodeMap_.find(key);
                if(it!= nodeMap_.end())
                {
                    removeNode(it->second);
                    nodeMap_.erase(it);
                }
            }

        private:
            void initializeList()
            {
                dummyHead_ = std::make_shared<LruNodeType>(Key(),Value());
                dummyTail_ = std::make_shared<LruNodeType>(Key(),Value());
                dummyHead_->next_ = dummyTail_;
                dummyTail_->prev_ = dummyHead_;
            }

            //更新已存在节点
            void updateExistingNode(NodePtr node, const Value& value)
            {
                node->setValue(value);
                moveToMostRecent(node);
            }

            //添加新节点
            void addNewNode(const Key& key, const Value& value)
            {

                //缓存满了，淘汰最久未使用 LRU
                if(nodeMap_.size() >= capacity_)
                {
                    evictLeastRecent();
                }

                //新建节点
                NodePtr newNode = std::make_shared<LruNodeType>(key,value); 
                //插入到链表尾部
                insertNode(newNode);
                nodeMap_[key] = newNode;
            }

            //把一个节点，移动到链表最末尾 = 标记为 “最近刚用过”
            void moveToMostRecent(NodePtr node)
            {
                removeNode(node);
                insertNode(node);
            }

            //从链表删除一个节点
            void removeNode(NodePtr node)
            {
                //判断节点有效
                if(!node->prev_.expired() && node->next_)
                {
                    auto prev = node->prev_.lock();
                    prev->next_ = node->next_;
                    node->next_->prev_ = prev;
                    node->next_ = nullptr;
                }
            }   

            //插入到尾部 
            void insertNode(NodePtr node)
            {
                node->next_ = dummyTail_;
                node->prev_ = dummyTail_->prev_;
                dummyTail_->prev_.lock()->next_ = node;
                dummyTail_->prev_ = node;
            }   

            //淘汰 删除最久节点
            void evictLeastRecent()
            {
                NodePtr leastRecent = dummyHead_->next_;
                removeNode(leastRecent);
                nodeMap_.erase(leastRecent->getKey());
            }  
            
            private:
            int capacity_;
            NodeMap nodeMap_;
            std::mutex mutex_;
            NodePtr dummyHead_;
            NodePtr dummyTail_;
        };

        template<typename Key,typename Value>
        class YLruKCache :public YLruCache<Key,Value>
        {
            public:

            //构造：主缓存容量 + 历史缓存容量 + K值
            //K:访问K次才进入主缓存
            YLruKCache(int capacity,int historyCapacity,int k)
            :YLruCache<Key,Value>(capacity)//初始化主缓存
            //历史访问记录
            ,historyList_(std::make_unique<YLruCache<Key,size_t>>(historyCapacity))
            ,k_(k)
            {}

            Value get(Key key) override
            {
                Value value{};
                //先查主缓存
                bool inMainCache = YLruCache<Key,Value>::get(key,value);

                //记录访问次数+1
                size_t historyCount = historyList_->get(key);
                historyCount++;
                historyList_->put(key,historyCount);

                //命中主缓存直接返回    
                if(inMainCache)
                {
                    return value;
                }

                //访问次数达到 K，加入主缓存
                if(historyCount >= k_)
                {
                    auto it = historyValueMap_.find(key);
                    if(it != historyValueMap_.end())
                    {
                        Value storedValue = it ->second;
                        historyList_->remove(key);
                        historyValueMap_.erase(it);
                        //加入主缓存
                        YLruCache<Key,Value>::put(key,storedValue);
                        return storedValue;
                    }
                }
                return value;
            }
            //重写put
            void put(Key key,Value value) override
            {
                Value existingValue{};
                //查主缓存是否存在
                bool inMainCache = YLruCache<Key,Value>::get(key,existingValue);
                if(inMainCache)
                {
                    //存在直接更新
                    YLruCache<Key,Value>::put(key,value);
                    return;
                }

                //不存在，记录历史访问次数
                size_t historyCount = historyList_->get(key);
                historyCount++;
                historyList_->put(key,historyCount);
                historyValueMap_[key] = value;

                //达到K次才进主缓存
                if(historyCount>=k_)
                {
                    historyList_->remove(key);
                    historyValueMap_.erase(key);
                    YLruCache<Key,Value>::put(key,value);
                }
            }
            private:
            int k_;
            std::unique_ptr<YLruCache<Key,size_t>>historyList_;
            std::unordered_map<Key,Value> historyValueMap_;
        };


        //YHashLruCaches: 分片LRU（高并发优化）
        //将缓存分成N片 每片一把锁，降低锁冲突
        //原理： 原来一个锁 现在N个锁 多线程同时操作不同分片，速度更快
        template<typename Key,typename Value>
        class YHashLruCaches
        {
            public:
            //构造 总容量+分片数量
            YHashLruCaches(size_t capacity,int sliceNum)
            :capacity_(capacity)
            //分片数量：用户传的>0就用，否则用CPU核心数
            ,sliceNum_(sliceNum >0 ?sliceNum :std::thread::hardware_concurrency())
            {
                //计算每个分片的容量
                //总容量/分片数
                size_t sliceSize = std::ceil(capacity / static_cast<double>(sliceNum_));
                //创建N个LRU缓存分片
                for(int i=0;i<sliceNum_;i++)
                {
                    lruSliceCaches_.emplace_back(std::make_unique<YLruCache<Key,Value>>(sliceSize));
                }
            }


            //根据Key的hash值路由到对应分片
            void put(Key key,Value value)
            {
                //哈希取模 ：确定key存在哪个分片
                size_t sliceIndex = Hash(key) % sliceNum_;
                //对应分片执行put
                lruSliceCaches_[sliceIndex]->put(key,value);
            }

            bool get(Key key,Value&value)
            {
                size_t sliceIndex = Hash(key) % sliceNum_;
                return lruSliceCaches_[sliceIndex]->get(key,value);
            }

            Value get(Key key)
            {
                Value value;
                get(key,value);
                return value;
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
            std::vector<std::unique_ptr<YLruCache<Key,Value>>>lruSliceCaches_;//分片缓存
        };

    }

