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
        void setValue(const ValueO& value){value_=value;}
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
                initalizeList();
            }

            ~YLruCache()override=default;

        private:
            void initializeList();

            int capacity_;
            NodeMap nodoeMap_;
            std::mutex mutex_;
            NodePtr dummyHead_;
            NodePtr dummyTail_;
    };
}