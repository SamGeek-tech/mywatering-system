using Backend.Lib.Model;
using Backend.Lib.Storage;
using Microsoft.Azure.Functions.Worker;
using Microsoft.Extensions.Logging;
using System.Text.Json;

namespace Backend.Functions.Functions
{
    public class IoTHubIngestFunction
    {
        private readonly IStorage _storage;
        private readonly ILogger _logger;

        public IoTHubIngestFunction(IStorage storage, ILoggerFactory loggerFactory)
        {
            _storage = storage ?? throw new ArgumentNullException(nameof(storage));
            _logger = loggerFactory.CreateLogger<IoTHubIngestFunction>();
        }

        [Function("IoTHubIngestFunction")]
        [SignalROutput(HubName = "%SIGNALR_HUB%")]
        public async Task<IEnumerable<SignalRMessageAction>> RunAsync(
    [EventHubTrigger("%IOTHUB_EVENTHUB_NAME%", Connection = "IoTHubConnectionString", IsBatched = true)]
    IReadOnlyList<string> messages,
    FunctionContext context)
        {
            var actions = new List<SignalRMessageAction>();
            _logger.LogInformation("IoT Hub/EventHub batch received: {Count} messages", messages?.Count ?? 0);

            foreach (var message in messages)
            {
                try
                {
                    var payload = JsonSerializer.Deserialize<TelemetryPayload>(message, new JsonSerializerOptions
                    {
                        PropertyNameCaseInsensitive = true
                    });

                    if (payload == null)
                    {
                        _logger.LogWarning("Telemetry payload was null");
                        continue;
                    }

                    if (string.IsNullOrWhiteSpace(payload.DeviceId))
                    {
                        _logger.LogWarning("Telemetry missing DeviceId");
                        continue;
                    }

                    payload.Timestamp = payload.Timestamp == default ? DateTime.UtcNow : payload.Timestamp;

                    await _storage.InsertTimeseriesAsync(payload);
                    await _storage.UpsertLatestAsync(payload);

                    _logger.LogInformation("Stored telemetry for device {DeviceId} at {Timestamp}", payload.DeviceId, payload.Timestamp);

                    // Prepare a SignalR broadcast for this payload
                    actions.Add(new SignalRMessageAction("telemetryReceived")
                    {
                        GroupName = $"device:{payload.DeviceId}",
                        Arguments = new[] { payload }
                    });
                }
                catch (JsonException jex)
                {
                    _logger.LogWarning(jex, "Failed to parse telemetry JSON: {Message}", message);
                }
                catch (Exception ex)
                {
                    _logger.LogError(ex, "Error processing telemetry message");
                }
            }

            return actions;
        }

    }
}