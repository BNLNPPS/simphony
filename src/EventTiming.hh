#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__linux__)
#include <time.h>
#endif

struct EventTimingMetadata
{
    std::string geometry;
    std::string config;
    std::string macro;
    std::string simphony_version;
    std::string geant4_version;
    std::string primary_particle{"opticalphoton"};
    double      primary_momentum_gev_c{0.0};
    int         primary_multiplicity{0};
    long        random_seed{-1};
    std::string gpu_name;
    int         gpu_device_id{-1};
    std::uint64_t gpu_memory_bytes{0};
    int         cuda_driver_version{0};
    int         cuda_runtime_version{0};
};

class EventTimingRecorder
{
  public:
    explicit EventTimingRecorder(std::filesystem::path output = {}) : output_(std::move(output)) {}

    bool enabled() const { return !output_.empty(); }

    void BeginRun()
    {
        rows_.clear();
        run_origin_ = WallClock::now();
    }

    void BeginEvent(int event_id)
    {
        if (!enabled())
            return;

        rows_.emplace_back();
        EventTiming& row = rows_.back();
        row.event_id = event_id;
        row.start_time = row.cpu_pre_start_time = WallNow();
        row.start_offset_s = row.cpu_pre_start_offset_s = OffsetNow();
        row.process_cpu_start_s = ProcessCpuNow();
        row.thread_cpu_start_s = ThreadCpuNow();
        active_ = &row;
    }

    void SubmitGpu(std::int64_t num_gensteps, std::int64_t num_photons)
    {
        if (!active_)
            return;

        active_->num_gensteps = num_gensteps;
        active_->num_photons = num_photons;
        active_->cpu_pre_end_time = active_->gpu_submit_time = active_->gpu_wait_start_time = WallNow();
        active_->cpu_pre_end_offset_s = active_->gpu_submit_offset_s = active_->gpu_wait_start_offset_s = OffsetNow();
        active_->process_cpu_pre_s = ProcessCpuNow() - active_->process_cpu_start_s;
        active_->thread_cpu_pre_s = ThreadCpuNow() - active_->thread_cpu_start_s;
    }

    void BeginGpu()
    {
        if (!active_)
            return;

        active_->gpu_start_time = WallNow();
        active_->gpu_start_offset_s = OffsetNow();
    }

    void EndGpu()
    {
        if (!active_)
            return;

        active_->gpu_end_time = active_->gpu_wait_end_time = active_->cpu_post_start_time = WallNow();
        active_->gpu_end_offset_s = active_->gpu_wait_end_offset_s = active_->cpu_post_start_offset_s = OffsetNow();
        active_->process_cpu_post_start_s = ProcessCpuNow();
        active_->thread_cpu_post_start_s = ThreadCpuNow();
    }

    void EndEvent(std::size_t num_gpu_hits, std::size_t num_g4_hits)
    {
        if (!active_)
            return;

        active_->num_gpu_hits = num_gpu_hits;
        active_->num_g4_hits = num_g4_hits;
        active_->end_time = active_->cpu_end_time = active_->cpu_post_end_time = WallNow();
        active_->end_offset_s = active_->cpu_end_offset_s = active_->cpu_post_end_offset_s = OffsetNow();
        active_->process_cpu_post_s = ProcessCpuNow() - active_->process_cpu_post_start_s;
        active_->thread_cpu_post_s = ThreadCpuNow() - active_->thread_cpu_post_start_s;
        active_ = nullptr;
    }

    void Write(const EventTimingMetadata& metadata) const
    {
        if (!enabled())
            return;

        if (!output_.parent_path().empty())
            std::filesystem::create_directories(output_.parent_path());

        WriteCsv(metadata);
        WriteManifest(metadata);
    }

    const std::filesystem::path& output() const { return output_; }

  private:
    using WallClock = std::chrono::steady_clock;

    struct EventTiming
    {
        int          event_id{-1};
        std::int64_t num_gensteps{0};
        std::int64_t num_photons{0};
        std::size_t  num_gpu_hits{0};
        std::size_t  num_g4_hits{0};

        double start_time{0.0};
        double end_time{0.0};
        double cpu_pre_start_time{0.0};
        double cpu_pre_end_time{0.0};
        double gpu_submit_time{0.0};
        double gpu_start_time{0.0};
        double gpu_end_time{0.0};
        double gpu_wait_start_time{0.0};
        double gpu_wait_end_time{0.0};
        double cpu_post_start_time{0.0};
        double cpu_post_end_time{0.0};
        double cpu_end_time{0.0};

        double start_offset_s{0.0};
        double end_offset_s{0.0};
        double cpu_pre_start_offset_s{0.0};
        double cpu_pre_end_offset_s{0.0};
        double gpu_submit_offset_s{0.0};
        double gpu_start_offset_s{0.0};
        double gpu_end_offset_s{0.0};
        double gpu_wait_start_offset_s{0.0};
        double gpu_wait_end_offset_s{0.0};
        double cpu_post_start_offset_s{0.0};
        double cpu_post_end_offset_s{0.0};
        double cpu_end_offset_s{0.0};

        double process_cpu_start_s{0.0};
        double process_cpu_post_start_s{0.0};
        double process_cpu_pre_s{0.0};
        double process_cpu_post_s{0.0};
        double thread_cpu_start_s{0.0};
        double thread_cpu_post_start_s{0.0};
        double thread_cpu_pre_s{0.0};
        double thread_cpu_post_s{0.0};
    };

    static double ProcessCpuNow()
    {
        return static_cast<double>(std::clock()) / static_cast<double>(CLOCKS_PER_SEC);
    }

    static double ThreadCpuNow()
    {
#if defined(__linux__)
        timespec value{};
        if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) == 0)
            return static_cast<double>(value.tv_sec) + static_cast<double>(value.tv_nsec) * 1e-9;
#endif
        return ProcessCpuNow();
    }

    static double WallNow()
    {
        return std::chrono::duration<double>(WallClock::now().time_since_epoch()).count();
    }

    double OffsetNow() const
    {
        return std::chrono::duration<double>(WallClock::now() - run_origin_).count();
    }

    static std::string JsonEscape(const std::string& value)
    {
        std::ostringstream escaped;
        for (char c : value)
        {
            switch (c)
            {
                case '\\': escaped << "\\\\"; break;
                case '"': escaped << "\\\""; break;
                case '\n': escaped << "\\n"; break;
                case '\r': escaped << "\\r"; break;
                case '\t': escaped << "\\t"; break;
                default: escaped << c; break;
            }
        }
        return escaped.str();
    }

    static std::string UtcNow()
    {
        const std::time_t now = std::time(nullptr);
        std::tm           utc{};
#if defined(_WIN32)
        gmtime_s(&utc, &now);
#else
        gmtime_r(&now, &utc);
#endif
        std::ostringstream out;
        out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
        return out.str();
    }

    static double Duration(double start, double end) { return std::max(0.0, end - start); }

    void WriteCsv(const EventTimingMetadata& metadata) const
    {
        std::ofstream csv(output_);
        if (!csv)
            throw std::runtime_error("Unable to open event timing output: " + output_.string());

        csv << "scenario,dispatch_mode,event_id,"
               "start_time,end_time,cpu_start_time,cpu_end_time,cpu_pre_start_time,cpu_pre_end_time,"
               "gpu_submit_time,gpu_start_time,gpu_end_time,gpu_wait_start_time,gpu_wait_end_time,"
               "cpu_post_start_time,cpu_post_end_time,"
               "start_offset_s,end_offset_s,cpu_start_time_offset_s,cpu_end_time_offset_s,"
               "cpu_pre_start_time_offset_s,cpu_pre_end_time_offset_s,gpu_submit_time_offset_s,"
               "gpu_start_time_offset_s,gpu_end_time_offset_s,gpu_wait_start_time_offset_s,"
               "gpu_wait_end_time_offset_s,cpu_post_start_time_offset_s,"
               "cpu_post_end_time_offset_s,runtime_s,cpu_pre_runtime_s,cpu_post_runtime_s,cpu_runtime_s,"
               "gpu_queue_delay_s,gpu_runtime_s,gpu_wait_runtime_s,"
               "primary_particle,primary_momentum_gev_c,primary_multiplicity,"
               "num_gensteps,num_photons,num_gpu_hits,num_g4_hits,process_cpu_pre_s,process_cpu_post_s,"
               "process_cpu_s,thread_cpu_pre_s,thread_cpu_post_s,thread_cpu_s\n";

        csv << std::setprecision(12);
        for (const EventTiming& row : rows_)
        {
            const double cpu_pre_runtime = Duration(row.cpu_pre_start_time, row.cpu_pre_end_time);
            const double cpu_post_runtime = Duration(row.cpu_post_start_time, row.cpu_post_end_time);
            const double gpu_runtime = Duration(row.gpu_start_time, row.gpu_end_time);
            const double gpu_queue = Duration(row.gpu_submit_time, row.gpu_start_time);
            const double gpu_wait_runtime = Duration(row.gpu_wait_start_time, row.gpu_wait_end_time);

            csv << "simg4ox_blocking,blocking," << row.event_id << ',' << row.start_time << ',' << row.end_time << ','
                << row.cpu_pre_start_time << ','
                << row.cpu_end_time << ',' << row.cpu_pre_start_time << ',' << row.cpu_pre_end_time << ','
                << row.gpu_submit_time << ',' << row.gpu_start_time << ',' << row.gpu_end_time << ','
                << row.gpu_wait_start_time << ',' << row.gpu_wait_end_time << ',' << row.cpu_post_start_time << ','
                << row.cpu_post_end_time << ',' << row.start_offset_s << ','
                << row.end_offset_s << ',' << row.cpu_pre_start_offset_s << ',' << row.cpu_end_offset_s << ','
                << row.cpu_pre_start_offset_s << ',' << row.cpu_pre_end_offset_s << ',' << row.gpu_submit_offset_s
                << ',' << row.gpu_start_offset_s << ',' << row.gpu_end_offset_s << ','
                << row.gpu_wait_start_offset_s << ',' << row.gpu_wait_end_offset_s << ','
                << row.cpu_post_start_offset_s << ',' << row.cpu_post_end_offset_s << ','
                << Duration(row.start_time, row.end_time) << ',' << cpu_pre_runtime << ',' << cpu_post_runtime << ','
                << cpu_pre_runtime + cpu_post_runtime << ',' << gpu_queue << ',' << gpu_runtime << ','
                << gpu_wait_runtime << ',' << metadata.primary_particle << ',' << metadata.primary_momentum_gev_c
                << ',' << metadata.primary_multiplicity << ',' << row.num_gensteps << ',' << row.num_photons << ','
                << row.num_gpu_hits << ',' << row.num_g4_hits << ',' << row.process_cpu_pre_s << ','
                << row.process_cpu_post_s << ','
                << row.process_cpu_pre_s + row.process_cpu_post_s << ',' << row.thread_cpu_pre_s << ','
                << row.thread_cpu_post_s << ',' << row.thread_cpu_pre_s + row.thread_cpu_post_s << '\n';
        }
    }

    void WriteManifest(const EventTimingMetadata& metadata) const
    {
        std::filesystem::path manifest = output_;
        manifest.replace_extension(".manifest.json");
        std::ofstream json(manifest);
        if (!json)
            throw std::runtime_error("Unable to open event timing manifest: " + manifest.string());

        json << "{\n"
             << "  \"schema_version\": 2,\n"
             << "  \"created_utc\": \"" << UtcNow() << "\",\n"
             << "  \"application\": \"simg4ox\",\n"
             << "  \"dispatch_mode\": \"blocking\",\n"
             << "  \"clock\": \"std::chrono::steady_clock\",\n"
             << "  \"geometry\": \"" << JsonEscape(metadata.geometry) << "\",\n"
             << "  \"config\": \"" << JsonEscape(metadata.config) << "\",\n"
             << "  \"macro\": \"" << JsonEscape(metadata.macro) << "\",\n"
             << "  \"simphony_version\": \"" << JsonEscape(metadata.simphony_version) << "\",\n"
             << "  \"geant4_version\": \"" << JsonEscape(metadata.geant4_version) << "\",\n"
             << "  \"primary_particle\": \"" << JsonEscape(metadata.primary_particle) << "\",\n"
             << "  \"primary_momentum_gev_c\": " << metadata.primary_momentum_gev_c << ",\n"
             << "  \"primary_multiplicity\": " << metadata.primary_multiplicity << ",\n"
             << "  \"random_seed\": " << metadata.random_seed << ",\n"
             << "  \"gpu_name\": \"" << JsonEscape(metadata.gpu_name) << "\",\n"
             << "  \"gpu_device_id\": " << metadata.gpu_device_id << ",\n"
             << "  \"gpu_memory_bytes\": " << metadata.gpu_memory_bytes << ",\n"
             << "  \"cuda_driver_version\": " << metadata.cuda_driver_version << ",\n"
             << "  \"cuda_runtime_version\": " << metadata.cuda_runtime_version << ",\n"
             << "  \"event_count\": " << rows_.size() << "\n"
             << "}\n";
    }

    std::filesystem::path   output_;
    WallClock::time_point   run_origin_{WallClock::now()};
    std::vector<EventTiming> rows_;
    EventTiming*             active_{nullptr};
};
