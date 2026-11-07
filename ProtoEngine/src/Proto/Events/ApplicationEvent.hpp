#pragma once

#include "Event.hpp"

namespace Proto {

	// Example event types
	class WindowResizeEvent : public Event {
	private:
		int width;
		int height;

	public:
		WindowResizeEvent(int w, int h) : width(w), height(h) {}

		int GetWidth() const { return width; }
		int GetHeight() const { return height; }

		DEFINE_EVENT_TYPE_CATEGORY(WindowResizeEvent,
			static_cast<int>(EventCategory::Application) |
			static_cast<int>(EventCategory::Window))
	};

	class WindowCloseEvent : public Event
	{
	public:
		WindowCloseEvent() = default;

		DEFINE_EVENT_TYPE_CATEGORY(WindowCloseEvent,
			static_cast<int>(EventCategory::Application) |
			static_cast<int>(EventCategory::Window))
	};
}