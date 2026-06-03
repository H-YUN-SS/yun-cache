#pragma once
#include "../YICachePolicy.h"
#include "YArcLruPart.h"
#include "YArcLfuPart.h"
#include <memory>


namespace YCache
{
    //YArcCache
    //组合LRU + LFU 动态调整两区容量，实现自适应缓存
    //结构
    //  成员：总容量、升级阈值、LRU组件、LFU组件
    //  方法：put/get(对外接口)、checkGhostCaches(自适应调容)
    template<typename Key,typename Value>
    class YArcCache:public YICachePolicy<Key,Value>
    {
        public:
        //transformThreshold: LRU升级到LFU的访问次数阈值
        explicit YArcCache(size_t capacity = 10,size_t transformThreshold = 2)
        :capacity_(capacity)
        //初始化升级阈值
        ,transformThreshold_(transformThreshold)
        //创建LRU分区对象
        ,lruPart_(std::make_unique<ArcLruPart<Key,Value>>(capacity,transformThreshold))
        //创建LFU分区对象
        ,lfuPart_(std::make_unique<ArcLfuPart<Key,Value>>(capacity,transformThreshold))
        {}

        ~YArcCache() override = default;

        void put(Key key,Value value)override
        {
            //第一步：检查key是否在幽灵缓存，动态调整LRU/LFU容量
            checkGhostCaches(key);

            //判断key是否以及存在LFU热点区
            bool inLfu = lfuPart_->contain(key);
            //优先写入/更新LRU区
            lruPart_->put(key,value);
            //如果key在LFU区 同步更新LFU区数据
            if(inLfu)
            {
                lfuPart_->put(key,value);
            }
        }

        //对外接口 读取缓存（输出参数 返回是否命中）
        bool get(Key key,Value&value)override{
            //第一步：检查key是否在幽灵缓存，动态调整LRU/LFU容量
            checkGhostCaches(key);

            //标记 是否需要从LRU升级到LFU
            bool shouldTransfrom = false;
            //先尝试从LRU区读取
            if(lruPart_->get(key,value,shouldTransfrom))
            {
                //达到升级阈值，将数据移入LFU热点区
                if(shouldTransfrom)
                {
                    lfuPart_->put(key,value);
                }
                return true;
            }
            //LRU未命中 尝试从LFU区读取
            return lfuPart_->get(key,value);
        }

        //对外接口 简化get 直接返回value
        Value get(Key key)override
        {
            //定义value接受结果
            Value value{};
            //调用带输出参数的get
            get(key,value);
            return value;
        }

        private:
        //核心功能 检查幽灵缓存，动态自适应调整LRU/LFU容量
        //命中LRU幽灵 ->扩大LRU,缩小LFU
        //命中LFU幽灵 ->扩大LFU,缩小LRU
        bool checkGhostCaches(Key key)
        {
            //标记是否命中幽灵缓存
            bool inGhost = false;
            //命中 LRU 幽灵缓存
            if(lruPart_->checkGhost(key))
            {
                //LFU容量-1
                if(lruPart_->decreaseCapacity())
                {
                    lfuPart_->increaseCapacity();
                }
                inGhost = true;
            }
            //命中LFU幽灵缓存
            else if(lfuPart_->checkGhost(key))
            {
                //LRU容量-1
                if(lruPart_->decreaseCapacity())
                {
                    lfuPart_->increaseCapacity();
                }
                inGhost = true;
            }
            return inGhost;
        }





        private:
        size_t capacity_;
        //LRU升级LFU阈值
        size_t transformThreshold_;
        //LRU组件 (智能指针)
        std::unique_ptr<ArcLruPart<Key,Value>>lruPart_;
        //LFU组件
        std::unique_ptr<ArcLfuPart<Key,Value>>lfuPart_;

    };
}