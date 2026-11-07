#pragma once

#include "Event.hpp"

namespace Proto {

	class KeyPressEvent : public Event {
	private:
		int keyCode;
		bool repeat;

	public:
		KeyPressEvent(int key, bool isRepeat) : keyCode(key), repeat(isRepeat) {}

		int GetKeyCode() const { return keyCode; }
		bool IsRepeat() const { return repeat; }

		DEFINE_EVENT_TYPE_CATEGORY(KeyPressEvent,
			static_cast<int>(EventCategory::Keyboard) |
			static_cast<int>(EventCategory::Input))
	};

	class KeyReleasedEvent : public Event {
	private:
		int keyCode;

	public:
		KeyReleasedEvent(int key) : keyCode(key) {}

		int GetKeyCode() const { return keyCode; }

		DEFINE_EVENT_TYPE_CATEGORY(KeyReleasedEvent,
			static_cast<int>(EventCategory::Keyboard) |
			static_cast<int>(EventCategory::Input))
	};

	class KeyTypedEvent : public Event
	{
	private:
		int keyCode;
	public:
		KeyTypedEvent(int keycode)
			: keyCode(keycode) {}

		int GetKeyCode() const { return keyCode; }

		DEFINE_EVENT_TYPE_CATEGORY(KeyTypedEvent,
			static_cast<int>(EventCategory::Keyboard) |
			static_cast<int>(EventCategory::Input))
	};
}