using System.Threading.Tasks;
using Backend.Lib.Model;
using System.Collections.Generic;
using System;

namespace Backend.Lib.Storage
{
    public interface IStorage
    {
        Task InsertTimeseriesAsync(TelemetryPayload payload);
        Task UpsertLatestAsync(TelemetryPayload payload);
        Task<List<TelemetryPayload>> QueryTimeseriesAsync(string deviceId, DateTime from, DateTime to);
        Task<TelemetryPayload?> GetLatestAsync(string deviceId);
        Task<List<TelemetryPayload>> ListDevicesAsync();

        // OTA status management
        Task SetOtaStatusAsync(string deviceId, OtaStatus status);
        Task<OtaStatus?> GetOtaStatusAsync(string deviceId);
    }
}