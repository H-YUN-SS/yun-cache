#pragma once
namespace YCache
{
    template<typename Key,typename Value>
    class YICachePolicy
    {
        public:
        virtual ~YICachePolicy(){};

        virtual void put(Key key,Value value)=0;
        virtual void get(Key Key,Value& Value)=0;
        virtual Value get(Key key)=0;
    };
}