using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;
using Backend.Lib.Model;

namespace Backend.Lib.Storage
{
    // Simple file-based storage for PoC and local testing.
    public class FileStorage : IStorage
    {
        private readonly string _basePath;
        private readonly JsonSerializerOptions _jsonOptions = new JsonSerializerOptions
        {
            WriteIndented = false
        };

        public FileStorage(string? basePath = null)
        {
            _basePath = basePath ?? Path.Combine(AppContext.BaseDirectory, "data");
            Directory.CreateDirectory(_basePath);
            Directory.CreateDirectory(Path.Combine(_basePath, "timeseries"));
            Directory.CreateDirectory(Path.Combine(_basePath, "latest"));
        }

        public async Task InsertTimeseriesAsync(TelemetryPayload payload)
        {
            var deviceDir = Path.Combine(_basePath, "timeseries", payload.DeviceId);
            Directory.CreateDirectory(deviceDir);
            var filename = Path.Combine(deviceDir, payload.Timestamp.ToString("yyyyMMddTHHmmssfff") + ".json");
            await File.WriteAllTextAsync(filename, JsonSerializer.Serialize(payload, _jsonOptions));
        }

        public async Task UpsertLatestAsync(TelemetryPayload payload)
        {
            var file = Path.Combine(_basePath, "latest", payload.DeviceId + ".json");
            await File.WriteAllTextAsync(file, JsonSerializer.Serialize(payload, _jsonOptions));
        }

        public async Task<List<TelemetryPayload>> QueryTimeseriesAsync(string deviceId, DateTime from, DateTime to)
        {
            var deviceDir = Path.Combine(_basePath, "timeseries", deviceId);
            if (!Directory.Exists(deviceDir)) return new List<TelemetryPayload>();
            var files = Directory.EnumerateFiles(deviceDir, "*.json");
            var results = new List<TelemetryPayload>();
            foreach (var f in files)
            {
                try
                {
                    var text = await File.ReadAllTextAsync(f);
                    var p = JsonSerializer.Deserialize<TelemetryPayload>(text);
                    if (p != null && p.Timestamp >= from && p.Timestamp <= to)
                        results.Add(p);
                }
                catch { }
            }
            return results.OrderBy(x => x.Timestamp).ToList();
        }

        public async Task<TelemetryPayload?> GetLatestAsync(string deviceId)
        {
            var file = Path.Combine(_basePath, "latest", deviceId + ".json");
            if (!File.Exists(file)) return null;
            var text = await File.ReadAllTextAsync(file);
            return JsonSerializer.Deserialize<TelemetryPayload>(text);
        }

        public async Task<List<TelemetryPayload>> ListDevicesAsync()
        {
            var latestDir = Path.Combine(_basePath, "latest");
            var results = new List<TelemetryPayload>();
            if (!Directory.Exists(latestDir)) return results;
            var files = Directory.EnumerateFiles(latestDir, "*.json");
            foreach (var f in files)
            {
                try
                {
                    var text = await File.ReadAllTextAsync(f);
                    var p = JsonSerializer.Deserialize<TelemetryPayload>(text);
                    if (p != null) results.Add(p);
                }
                catch { }
            }
            return results.OrderBy(x => x.DeviceId).ToList();
        }

        public async Task SetOtaStatusAsync(string deviceId, OtaStatus status)
        {
            var file = Path.Combine(_basePath, "latest", $"ota-{deviceId}.json");
            await File.WriteAllTextAsync(file, JsonSerializer.Serialize(status, _jsonOptions));
        }

        public async Task<OtaStatus?> GetOtaStatusAsync(string deviceId)
        {
            var file = Path.Combine(_basePath, "latest", $"ota-{deviceId}.json");
            if (!File.Exists(file)) return null;
            var text = await File.ReadAllTextAsync(file);
            return JsonSerializer.Deserialize<OtaStatus>(text);
        }
    }
}