#pragma once

#include<cstring>
#include<list>
#include<memory>
#include<mutex>
#include<unordered_map>

#include"YICachePolicy.h"

namespace YCache
{
    template<typename Key,typename Value>
    class LruNode
    {
        private:
        Key key_;
        Value value_;
        size_t accessCount_;//访问次数
        std::weak_ptr<LruNode>prev_;//上一个节点的指针 弱指针
        std::shared_ptr<LruNode> next_;
        public:
        LruNode(Key key,Value value)
        :key_(key)
        ,value_(value)
        ,accessCount_(1)
        {}
        Key getKey()const{return key_;}
        Value getValue()const{return value_;}
        void setValue(const Value& value){value_=value;}
        size_t getAccessCount()const{return accessCount_;}
        void incrementAccessCount(){accessCount_++;}
        template<typename K,typename V>
        friend class YLruCache;
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
            :capacity_(capacity)
            {
                initializeList();
            }

            ~YLruCache()override=default;

            void put(Key key,Value value) override;
            bool get(Key key,Value& value) override;
            Value get(Key key) override;

        private:
            void initializeList();
            //把一个节点，移动到链表最末尾 = 标记为 “最近刚用过”
            void moveToMostRecent(NodePtr node);

            void insertNode(NodePtr node);       
            void removeNode(NodePtr node);     
            void evictLeastRecent();  

            int capacity_;
            NodeMap nodeMap_;
            std::mutex mutex_;
            NodePtr dummyHead_;
            NodePtr dummyTail_;
    };
    template<typename Key,typename Value>
    void  YLruCache<Key,Value>::initializeList()
    {
        dummyHead_=std::make_shared<LruNodeType>(Key(),Value());
        dummyTail_=std::make_shared<LruNodeType>(Key(),Value());
        dummyHead_->next_=dummyTail_;
        dummyTail_->prev_=dummyHead_;
    }
    template<typename Key,typename Value>
    void YLruCache<Key,Value>::removeNode(NodePtr node)
    {
        if(node->prev_.expired()||!node->next_)
        {
            return;
        }
        auto prevNode=node->prev_.lock();
        prevNode->next_=node->next_;
        node->next_->prev_=prevNode;
        node->next_=nullptr;
        node->prev_.reset();
    }
    template<typename Key,typename Value>
    void YLruCache<Key,Value>::evictLeastRecent()
    {
        NodePtr oldestNode=dummyHead_->next_;
        if(oldestNode==dummyTail_)
        {
            return;
        }
        removeNode(oldsoldestNodetNode);
        NodeMap_.erase(oldestNode->getKey());
    }
    template<typename Key,typename Value>
    void YLruCache<Key,Value>::insertNode(NodePtr node)
    {
        if (!node || !dummyTail_) return;
            node->prev_ = dummyTail_->prev_;
            node->next_ = dummyTail_;

            auto prev_node = dummyTail_->prev_.lock();
            prev_node->next_ = node;

            dummyTail_->prev_ = node;
    }

    template<typename Key,typename Value>
    void YLruCache<Key,Value>::moveToMostRecent(NodePtr node)
    {
        removeNode(node);
        insertNode(node);
    }
    
    
    template<typename Key,typename Value>
    void YLruCache<Key,Value>::put(Key key,Value value)
    {
        if(capacity_<=0)
        {
            return ;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        auto it=nodeMap_.find(key);
        if(it!=nodeMap_.end())
        {
            it->second->setValue(value);
            moveToMostRecent(it->second);
            return;
        }
        if(nodeMap_.size()>=capacity_)
        {
            evictLeastRecent();
        }

        NodePtr newNode = std::make_shared<LruNodeType>(key, value);
        insertNode(newNode);
        nodeMap_[key] = newNode;


    }

    template<typename Key,typename Value>
    bool YLruCache<Key,Value>::get(Key key,Value& value)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = NodeMap_.find(key);
        if (it == NodeMap_.end()) {
            return false;
        }


        moveToMostRecent(it->second);
        value = it->second->getValue();
        return true;
    }

    template<typename Key,typename Value>
    Value YLruCache<Key,Value>::get(Key key)
    {
        Value val{};
        get(key, val);
        return val;
    }
}