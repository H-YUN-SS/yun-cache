#pragma once
namespace YCache
{
    template<typename Key,typename Value>
    class YICachePolicy
    {
        public:
        virtual ~YICachePolicy()=default;

        virtual void put(Key key,Value value)=0;
        virtual bool get(Key key,Value& value)=0;
        virtual Value get(Key key)=0;
    };
}