#pragma once

#include "YArcCacheNode.h" 
#include <unordered_map>
#include<mutex>

namespace YCache
{
    template<typename Key,typename Value>
    class ArcLruPart
    {
        public:
        using NodeType = ArcNode<Key,Value>;
        using NodePtr = std::shared_ptr<NodeType>;
        using NodeMap = std::unordered_map<Key,NodePtr>;

        explicit ArcLruPart(size_t capacity,size_t transformThreshold)
        :capacity_(capacity)
        ,ghostCapacity_(capacity)
        //LRU升级LFU阈值
        ,transformThreshold_(transformThreshold)
        {
            //初始化主链表+幽灵链表
            initializeLists();
        }
        
        //写入LRU缓存
        bool put(Key key,Value value)
        {
            if(capacity_ == 0 )
            {
                return false;
            }

            std::lock_guard<std::mutex>lock(mutex_);

            auto it = mainCache_.find(key);
            if(it!= mainCache_.end())
            {
                //key存在则更新
                return updateExistingNode(it->second,value);
            }
            //不存在就新增
            return addNewNode(key,value);
        }

        //读取LRU缓存
        //shouldTransform:输入参数，标记是否满足升级LFU条件
        bool get(Key key,Value& value,bool& shouldTransform)
        {
            std::lock_guard<std::mutex>lock(mutex_);
            auto it = mainCache_.find(key);
            if(it != mainCache_.end())
            {
                //更新访问状态，返回是否可升级
                shouldTransform = updateNodeAccess(it->second);
                value = it->second->getValue();
                return true;
            }
            return false;
        }

        //检查key是否在LRU幽灵缓存
        bool checkGhost(Key key)
        {
            auto it = ghostCache_.find(key);
            if(it != ghostCache_.end())
            {
                removeFromGhost(it->second);
                ghostCache_.erase(it);
                return true;
            }
            return false;
        }

        //增加LRU容量（ARC自适应）
        void increaseCapacity()
        {
            ++capacity_;
        }

        //减少LRU容量
        bool decreaseCapacity()
        {
            if(capacity_ <=0 )
            {
                return false;
            }

            if(mainCache_.size() == capacity_)
            {
                //缩容先淘汰
                evictLeastRecent();
            }
            --capacity_;
            return true;
        }


        private:
        //初始化两条双向链表：主缓存链表 + 幽灵缓存链表
        void initializeLists()
        {
            //主链表初始化
            mainHead_ = std::make_shared<NodeType>();
            mainTail_ = std::make_shared<NodeType>();
            mainHead_->next_= mainTail_;
            mainTail_->prev_= mainHead_;

            //幽灵链表初始化
            ghostHead_ = std::make_shared<NodeType>();
            ghostTail_ = std::make_shared<NodeType>();
            ghostHead_->next_= ghostTail_;
            ghostTail_->prev_= ghostHead_;
        }


        //更新已存在的节点
        bool updateExistingNode(NodePtr node,const Value&value)
        {
            node->setValue(value);
            //访问过的节点移到头部（标记为最新）
            moveToFront(node);
            return true;
        }

        //添加新节点
        bool addNewNode(const Key& key, const Value& value)
        {
            if(mainCache_.size() >= capacity_)
            {
                //满了淘汰最久未使用
                evictLeastRecent();
            }

            NodePtr newNode = std::make_shared<NodeType>(key,value);
            mainCache_[key] = newNode;
            //新节点插入头部
            addToFront(newNode);
            return true;
        }

        //更新节点访问：移到头部+访问次数+1
        //返回值：是否达到升级LFU阈值
        bool updateNodeAccess(NodePtr node)
        {
            moveToFront(node);
            node->incrementAccessCount();
            return node->getAccessCount() >=transformThreshold_;
        }

        //把节点移到链表头部
        void moveToFront(NodePtr node)
        {
            //从当前位置断开
            if(!node->prev_.expired()&&node->next_)
            {
                auto prev = node->prev_.lock();
                prev->next_=node->next_;
                node->next_->prev_=node->prev_;
                node->next_=nullptr;
            }
            //插入头部
            addToFront(node);
        }

        void addToFront(NodePtr node)
        {
            node->next_=mainHead_->next_;
            node->prev_=mainHead_;
            mainHead_->next_->prev_=node;
            mainHead_->next_=node;
        }


        //LRU淘汰链表尾部
        void evictLeastRecent()
        {
            //拿到队尾节点
            NodePtr leastRecent = mainTail_->prev_.lock();
            if(!leastRecent||leastRecent == mainHead_)
            {
                return;
            }

            //从主链表移除
            removeFromMain(leastRecent);

            if(ghostCache_.size() >= ghostCapacity_)
            {
                removeOldestGhost();
            }
            addToGhost(leastRecent);

            //从哈希表删除
            mainCache_.erase(leastRecent->getKey());
            

        }

        //从主链表移除节点
        void removeFromMain(NodePtr node)
        {
            if(!node->prev_.expired()&&node->next_)
            {
                auto prev = node->prev_.lock();
                prev->next_ = node->next_;
                node->next_->prev_=node->prev_;
                node->next_=nullptr;
            }
        }

        //从幽灵链表移除节点
        void removeFromGhost(NodePtr node)
        {
            if(!node->prev_.expired()&&node->next_)
            {
                auto prev = node->prev_.lock();
                prev->next_=node->next_;
                node->next_->prev_=node->prev_;
                node->next_=nullptr;
            }
        }

        //添加节点到幽灵缓存
        void addToGhost(NodePtr node)
        {
            //重置访问次数
            node->accessCount_=1;
            //插入幽灵链表头部
            node->next_=ghostHead_->next_;
            node->prev_=ghostHead_;
            ghostHead_->next_->prev_=node;
            ghostHead_->next_=node;

            ghostCache_[node->getKey()] = node;
        }

        //删除最老的幽灵节点
        void removeOldestGhost()
        {
            NodePtr oldestGhost = ghostTail_->prev_.lock();
            if(!oldestGhost||oldestGhost == ghostHead_)
            {
                return;
            }

            removeFromGhost(oldestGhost);
            ghostCache_.erase(oldestGhost->getKey());

        }

        private:
        //主缓存容量
        size_t capacity_;
        //幽灵缓存容量
        size_t ghostCapacity_;
        //访问次数阈值 （转到LFU）
        size_t transformThreshold_;
        std::mutex mutex_;

        //主缓存哈希表：key->节点
        NodeMap mainCache_;
        //幽灵缓存哈希表
        NodeMap ghostCache_;

        //主链表头尾
        NodePtr mainHead_;
        NodePtr mainTail_;
        //幽灵链表头尾
        NodePtr ghostHead_;
        NodePtr ghostTail_;

    };
    
}