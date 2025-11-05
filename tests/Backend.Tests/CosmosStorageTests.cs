using System;
using System.Threading.Tasks;
using Backend.Lib.Model;
using Backend.Lib.Storage;
using Xunit;

namespace Backend.Tests
{
    public class CosmosStorageTests
    {
        // This test is skipped by default. To run against a live Cosmos DB, set the COSMOS_CONNECTIONSTRING env var.
        [Fact(Skip = "Requires COSMOS_CONNECTIONSTRING environment variable for live integration test")]
        public async Task InsertAndQueryTimeseries_Cosmos_Works()
        {
            var conn = Environment.GetEnvironmentVariable("COSMOS_CONNECTIONSTRING");
            Assert.False(string.IsNullOrWhiteSpace(conn), "COSMOS_CONNECTIONSTRING must be set to run this test");

            var storage = new CosmosStorage(conn, databaseId: "test_watering_db");
            var deviceId = "test-cs-" + Guid.NewGuid().ToString("N").Substring(0, 8);
            var payload = new TelemetryPayload { DeviceId = deviceId, Timestamp = DateTime.UtcNow };
            payload.Sensors.Add(new SensorReading { Name = "moisture1", Value = 55.5 });

            await storage.InsertTimeseriesAsync(payload);
            await storage.UpsertLatestAsync(payload);

            var latest = await storage.GetLatestAsync(deviceId);
            Assert.NotNull(latest);
            Assert.Equal(deviceId, latest.DeviceId);

            var range = await storage.QueryTimeseriesAsync(deviceId, DateTime.UtcNow.AddMinutes(-5), DateTime.UtcNow.AddMinutes(5));
            Assert.NotEmpty(range);
        }
    }
}