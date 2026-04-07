#pragma once

#include<cstring>
#include<list>
#include<memory>
#include<mutex>
#include<unordered_map>

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
        
        bool get(Key key, Value& value) override;
        Value get(Key key) override;
        void remove(Key key);

    private:
        void initializeList();
        //把一个节点，移动到链表最末尾 = 标记为 “最近刚用过”
        void moveToMostRecent(NodePtr node);
        void insertNode(NodePtr node);       
        void removeNode(NodePtr node);     
        void evictLeastRecent();  
        void updateExistingNode(NodePtr node, const Value& value);
        void addNewNode(const Key& key, const Value& value);

        int capacity_;
        NodeMap nodeMap_;
        std::mutex mutex_;
        NodePtr dummyHead_;
        NodePtr dummyTail_;
    };

    template<typename Key,typename Value>
    void YLruCache<Key,Value>::initializeList()
    {
        dummyHead_ = std::make_shared<LruNodeType>(Key(), Value());
        dummyTail_ = std::make_shared<LruNodeType>(Key(), Value());
        dummyHead_->next_ = dummyTail_;
        dummyTail_->prev_ = dummyHead_;
    }

    template<typename Key,typename Value>
    void YLruCache<Key,Value>::removeNode(NodePtr node)
    {
        // 1111：对齐原仓库的条件判断逻辑（原仓库是“满足条件才执行”，而非“不满足则返回”）
        if (!node->prev_.expired() && node->next_)
        {
            auto prevNode = node->prev_.lock();
            prevNode->next_ = node->next_;
            node->next_->prev_ = prevNode;
            node->next_ = nullptr;
        }
    }

    template<typename Key,typename Value>
    void YLruCache<Key,Value>::evictLeastRecent()
    {
        NodePtr oldestNode = dummyHead_->next_;
        removeNode(oldestNode);
        nodeMap_.erase(oldestNode->getKey());
    }

    template<typename Key,typename Value>
    void YLruCache<Key,Value>::insertNode(NodePtr node)
    {
        node->next_ = dummyTail_;
        node->prev_ = dummyTail_->prev_;
        dummyTail_->prev_.lock()->next_ = node; 
        dummyTail_->prev_ = node;
    }

    template<typename Key,typename Value>
    void YLruCache<Key,Value>::moveToMostRecent(NodePtr node)
    {
        removeNode(node);
        insertNode(node);
    }

    template<typename Key,typename Value>
    void YLruCache<Key,Value>::updateExistingNode(NodePtr node, const Value& value)
    {
        node->setValue(value);
        moveToMostRecent(node);
    }

    template<typename Key,typename Value>
    void YLruCache<Key,Value>::addNewNode(const Key& key, const Value& value)
    {
        if (nodeMap_.size() >= capacity_)
        {
            evictLeastRecent();
        }

        NodePtr newNode = std::make_shared<LruNodeType>(key, value);
        insertNode(newNode);
        nodeMap_[key] = newNode;
    }

    template<typename Key,typename Value>
    bool YLruCache<Key,Value>::get(Key key, Value& value)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeMap_.find(key);
        if (it != nodeMap_.end())
        {
            moveToMostRecent(it->second);
            value = it->second->getValue();
            return true;
        }
        return false;
    }

    template<typename Key,typename Value>
    Value YLruCache<Key,Value>::get(Key key)
    {
        Value value{};
        get(key, value);
        return value;
    }

    template<typename Key,typename Value>
    void YLruCache<Key,Value>::remove(Key key)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeMap_.find(key);
        if (it != nodeMap_.end())
        {
            removeNode(it->second);
            nodeMap_.erase(it);
        }
    }
}