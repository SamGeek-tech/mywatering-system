using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Microsoft.Azure.Cosmos;
using Backend.Lib.Model;
using System.Text.Json;

namespace Backend.Lib.Storage
{
    // Cosmos DB implementation of IStorage. Requires environment variables or connection string
    // Usage: new CosmosStorage(connectionString, databaseId)
    public class CosmosStorage : IStorage, IAsyncDisposable
    {
        private readonly CosmosClient _client;
        private readonly Container _timeseriesContainer;
        private readonly Container _latestContainer;
        private readonly string _databaseId;

        public CosmosStorage(string connectionString, string databaseId = "watering_db", string timeseriesContainerId = "device_timeseries", string latestContainerId = "devices_latest")
        {
            // Use Gateway mode which is more firewall friendly (uses port 443)
            var options = new CosmosClientOptions
            {
                ConnectionMode = ConnectionMode.Gateway
            };
            _client = new CosmosClient(connectionString, options);
            _databaseId = databaseId;

            try
            {
                // Ensure DB and containers exist. In production handle RU and throughput accordingly.
                var dbResponse = _client.CreateDatabaseIfNotExistsAsync(_databaseId).GetAwaiter().GetResult();
                var database = dbResponse.Database;
                database.CreateContainerIfNotExistsAsync(timeseriesContainerId, "/deviceId").GetAwaiter().GetResult();
                database.CreateContainerIfNotExistsAsync(latestContainerId, "/deviceId").GetAwaiter().GetResult();

                // assign containers
                _timeseriesContainer = database.GetContainer(timeseriesContainerId);
                _latestContainer = database.GetContainer(latestContainerId);
            }
            catch (CosmosException ex)
            {
                throw new Exception($"Failed to connect to Cosmos DB. Ensure your IP address is allowed in the Azure Portal Firewall settings. Error: {ex.Message}", ex);
            }
        }

        public async Task InsertTimeseriesAsync(TelemetryPayload payload)
        {
            var item = new
            {
                id = payload.DeviceId + "-" + payload.Timestamp.ToString("o"),
                deviceId = payload.DeviceId,
                timestamp = payload.Timestamp,
                sensors = payload.Sensors,
                battery = payload.Battery,
                rssi = payload.Rssi,
                firmwareVersion = payload.FirmwareVersion
            };
            await _timeseriesContainer.CreateItemAsync(item, new PartitionKey(payload.DeviceId));
        }

        public async Task UpsertLatestAsync(TelemetryPayload payload)
        {
            var item = new
            {
                id = payload.DeviceId,
                deviceId = payload.DeviceId,
                lastSeen = payload.Timestamp,
                latest = payload,
            };
            await _latestContainer.UpsertItemAsync(item, new PartitionKey(payload.DeviceId));
        }

        public async Task<List<TelemetryPayload>> QueryTimeseriesAsync(string deviceId, DateTime from, DateTime to)
        {
            var sql = "SELECT c.timestamp, c.sensors, c.battery, c.rssi, c.firmwareVersion FROM c WHERE c.deviceId = @deviceId AND c.timestamp >= @from AND c.timestamp <= @to ORDER BY c.timestamp";
            var query = new QueryDefinition(sql)
                .WithParameter("@deviceId", deviceId)
                .WithParameter("@from", from)
                .WithParameter("@to", to);
            var iterator = _timeseriesContainer.GetItemQueryIterator<dynamic>(query, requestOptions: new QueryRequestOptions { PartitionKey = new PartitionKey(deviceId) });
            var results = new List<TelemetryPayload>();
            while (iterator.HasMoreResults)
            {
                var response = await iterator.ReadNextAsync();
                foreach (var r in response)
                {
                    try
                    {
                        var tp = new TelemetryPayload
                        {
                            DeviceId = deviceId,
                            Timestamp = (DateTime)r.timestamp,
                            Battery = r.battery != null ? (double?)r.battery : null,
                            Rssi = r.rssi != null ? (int?)r.rssi : null,
                            FirmwareVersion = r.firmwareVersion != null ? (string)r.firmwareVersion : null,
                            Sensors = ((IEnumerable<dynamic>)r.sensors).Select(s => new SensorReading { Name = (string)s.name, Type = (string?)s.type, Value = (double)s.value, Unit = (string?)s.unit }).ToList()
                        };
                        results.Add(tp);
                    }
                    catch { }
                }
            }
            return results.OrderBy(x => x.Timestamp).ToList();
        }

        public async Task<TelemetryPayload?> GetLatestAsync(string deviceId)
        {
            try
            {
                var response = await _latestContainer.ReadItemAsync<dynamic>(deviceId, new PartitionKey(deviceId));
                var r = response.Resource;
                if (r == null) return null;
                // latest is a subdocument containing the payload
                var latest = r.latest;
                if (latest == null) return null;
                var tp = JsonSerializerDeserialize(latest);
                return tp;
            }
            catch (CosmosException ex) when (ex.StatusCode == System.Net.HttpStatusCode.NotFound)
            {
                return null;
            }
        }

        public async Task<List<TelemetryPayload>> ListDevicesAsync()
        {
            var sql = "SELECT c.latest FROM c";
            var iterator = _latestContainer.GetItemQueryIterator<dynamic>(new QueryDefinition(sql));
            var results = new List<TelemetryPayload>();
            while (iterator.HasMoreResults)
            {
                var response = await iterator.ReadNextAsync();
                foreach (var r in response)
                {
                    try
                    {
                        var latest = r.latest;
                        if (latest == null) continue;
                        var tp = JsonSerializerDeserialize(latest);
                        if (tp != null) results.Add(tp);
                    }
                    catch { }
                }
            }
            return results.OrderBy(x => x.DeviceId).ToList();
        }

        private TelemetryPayload? JsonSerializerDeserialize(dynamic obj)
        {
            try
            {
                // Round-trip via JSON to map to TelemetryPayload POCO
                var json = JsonSerializer.Serialize(obj);
                return JsonSerializer.Deserialize<TelemetryPayload>(json);
            }
            catch
            {
                return null;
            }
        }

        public async ValueTask DisposeAsync()
        {
            _client?.Dispose();
            await Task.CompletedTask;
        }

        // OTA methods
        public async Task SetOtaStatusAsync(string deviceId, OtaStatus status)
        {
            var item = new
            {
                id = "ota-" + deviceId,
                deviceId = deviceId,
                status = status,
            };
            await _latestContainer.UpsertItemAsync(item, new PartitionKey(deviceId));
        }

        public async Task<OtaStatus?> GetOtaStatusAsync(string deviceId)
        {
            try
            {
                var response = await _latestContainer.ReadItemAsync<dynamic>("ota-" + deviceId, new PartitionKey(deviceId));
                var r = response.Resource;
                if (r == null) return null;
                var status = r.status;
                if (status == null) return null;
                var json = JsonSerializer.Serialize(status);
                return JsonSerializer.Deserialize<OtaStatus>(json);
            }
            catch (CosmosException ex) when (ex.StatusCode ==  System.Net.HttpStatusCode.NotFound)
            {
                return null;
            }
        }
    }
}