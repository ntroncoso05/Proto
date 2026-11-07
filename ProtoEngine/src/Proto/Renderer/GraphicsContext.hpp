#pragma once

namespace Proto {

	class GraphicsContext
	{
	public:
		~GraphicsContext() = default;
		virtual void Init() = 0;
		virtual void SwapBuffers() = 0;
	};

}