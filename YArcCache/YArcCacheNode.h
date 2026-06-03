#pragma once
#include<memory>

namespace YCache
{
    
    //缓存节点模板类
    //作用 存储key,value、访问次数、双向链表指针
    template<typename Key,typename Value>
    class ArcNode
    {
        private:

        Key key_;
        Value value_;
        //访问次数(LRU/LFU共用)
        size_t accessCount_;
        std::weak_ptr<ArcNode>prev_;
        std::shared_ptr<ArcNode>next_;
        //unique_ptr 不能被拷贝

        public:
        ArcNode():accessCount_(1),next_{}

        ArcNode(Key key,Value value)
        :key_(key)
        ,value_(value)
        ,accessCount_(1)
        ,next_(nullptr)
        {}

        Key getKey()const
        {
            return key_;
        }


        Value getValue()const
        {
            return value_;
        }

        //获取访问次数
        size_t getAccessCount() const
        {
            return accessCount_;
        }

        void setValue(const Value& value)
        {
            value_ = value;
        }

        //访问次数+1
        void incrementAccessCount()
        {
            ++accessCount_;
        }

        template<typename K,typename V>friend class ArcLruPart;
        template<typename K,typename V>friend class ArcLfuPart;
    };
}