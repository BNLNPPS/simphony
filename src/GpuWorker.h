#pragma once

namespace simphony
{
inline constexpr char GpuWorkerFlag[] = "--gpu-worker";

int RunGpuWorker(int argc, char** argv);
} // namespace simphony
