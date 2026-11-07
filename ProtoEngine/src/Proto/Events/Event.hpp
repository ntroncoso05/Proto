#pragma once
//#include <functional>

#include "Core/Core.hpp"
#include <string>
#include <sstream>

#include <queue>

namespace Proto {

    enum class EventCategory
    {
    	None = 0,
    	Application    = BIT(0),
    	Input          = BIT(1),
    	Keyboard       = BIT(2),
    	Mouse          = BIT(3),
    	MouseButton    = BIT(4),
        Window         = BIT(5)
    };
    
    // Base event class
    class Event {
    public:
        virtual ~Event() = default;

        virtual const char* GetType() const = 0;
        virtual Event* Clone() const = 0;
        virtual int GetCategoryFlags() const = 0;

        bool IsInCategory(EventCategory category) const {
            return GetCategoryFlags() & static_cast<int>(category);
        }

        // --- NEW: Handled State for Pipeline Routing ---
        bool Handled = false;
    };

// Enhanced macro to define event types with categories
#define DEFINE_EVENT_TYPE_CATEGORY(type, categoryFlags) \
    static const char* GetStaticType() { return #type; } \
    virtual const char* GetType() const override { return GetStaticType(); } \
    virtual Event* Clone() const override { return new type(*this); } \
    virtual int GetCategoryFlags() const override { return categoryFlags; }

    // Event listener interface
    class EventListener {
    public:
        virtual ~EventListener() = default;
        virtual void OnEvent(const Event& event) = 0;
    };

    // Event dispatcher
    class EventDispatcher {
    private:
        Event& m_Event; // Changed from 'const Event&' to allow modifying the 'Handled' state

    public:
        explicit EventDispatcher(Event& e) : m_Event(e) {}

        template<typename T, typename F>
        bool Dispatch(const F& handler) {
            // Only dispatch if the event hasn't already been consumed
            if (m_Event.Handled) return false;

            if (m_Event.GetType() == T::GetStaticType()) {
                // Execute handler and pass back its boolean result or set Handled inside
                handler(static_cast<const T&>(m_Event));

                // Once dispatched to a matching type, mark it handled to consume it
                m_Event.Handled = true;
                return true;
            }
            return false;
        }
    };

    // Enhanced event bus with filtering
    class EventBus {
    private:
        struct ListenerInfo {
            EventListener* listener;
            int categoryFilter;
        };

        std::vector<ListenerInfo> listeners;
        std::queue<std::unique_ptr<Event>> eventQueue;
        std::mutex queueMutex;
        bool immediateMode = true;

    public:
        void SetImmediateMode(bool immediate) {
            immediateMode = immediate;
        }

        void AddListener(EventListener* listener, int categoryFilter = -1) {
            listeners.push_back({ listener, categoryFilter });
        }

        void RemoveListener(EventListener* listener) {
            auto it = std::find_if(listeners.begin(), listeners.end(),
                [listener](const ListenerInfo& info) {
                    return info.listener == listener;
                });
            if (it != listeners.end()) {
                listeners.erase(it);
            }
        }

        void PublishEvent(Event& event) { // Removed const restriction
            if (immediateMode) {
                // Dispatch event immediately
                for (const auto& info : listeners) {
                    if (event.Handled) break; // Stop propagation early!

                    if (info.categoryFilter == -1 || (event.GetCategoryFlags() & info.categoryFilter)) {
                        info.listener->OnEvent(event);
                    }
                }
            }
            else {
                // Queue event for later processing
                std::lock_guard<std::mutex> lock(queueMutex);
                eventQueue.push(std::unique_ptr<Event>(event.Clone()));
            }
        }

        void ProcessEvents() {
            if (immediateMode) return;

            std::queue<std::unique_ptr<Event>> currentEvents;

            {
                std::lock_guard<std::mutex> lock(queueMutex);
                std::swap(currentEvents, eventQueue);
            }

            while (!currentEvents.empty()) {
                auto& event = *currentEvents.front();

                for (const auto& info : listeners) {
                    if (info.categoryFilter == -1 || (event.GetCategoryFlags() & info.categoryFilter)) {
                        info.listener->OnEvent(event);
                    }
                }

                currentEvents.pop();
            }
        }
    };

}