using System;
using System.IO;
using System.Threading.Tasks;
using Backend.Lib.Model;
using Backend.Lib.Storage;
using Xunit;

namespace Backend.Tests
{
    public class FileStorageTests
    {
        [Fact]
        public async Task InsertAndQueryTimeseries_Works()
        {
            var tmp = Path.Combine(Path.GetTempPath(), "fs_test_" + Guid.NewGuid().ToString());
            Directory.CreateDirectory(tmp);
            var storage = new FileStorage(tmp);
            var payload = new TelemetryPayload { DeviceId = "test1", Timestamp = DateTime.UtcNow };
            payload.Sensors.Add(new SensorReading { Name = "moisture1", Value = 42.5 });
            await storage.InsertTimeseriesAsync(payload);
            var results = await storage.QueryTimeseriesAsync("test1", DateTime.UtcNow.AddMinutes(-5), DateTime.UtcNow.AddMinutes(5));
            Assert.Single(results);
        }
    }
}