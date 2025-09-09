#pragma once

#include <chrono>

float GetTimeInSeconds()
{
	using namespace std::chrono;
	static auto start = steady_clock::now();
	auto now = steady_clock::now();
	return duration<float>(now - start).count();
}