using Backend.Lib.Model;
using Backend.Lib.Storage;
using Microsoft.Azure.Functions.Worker;
using Microsoft.Azure.Functions.Worker.Http;
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

        // LOCAL DEV: Use this HTTP trigger when running locally
        [Function("IoTHubIngestFunction")]
        [SignalROutput(HubName = "%SIGNALR_HUB%")]
        public async Task<IEnumerable<SignalRMessageAction>> RunLocal(
            [HttpTrigger(AuthorizationLevel.Anonymous, "post", Route = "telemetry")] HttpRequestData req,
            FunctionContext context)
        {
            _logger.LogInformation("LOCAL DEV: HTTP trigger received telemetry");

            string requestBody = await new StreamReader(req.Body).ReadToEndAsync();
            var messages = new[] { requestBody };
            return await ProcessMessagesAsync(messages);
        }

        // PRODUCTION: This is used when deployed to Azure
        [Function("IoTHubIngestFunction_Prod")]
        [SignalROutput(HubName = "%SIGNALR_HUB%")]
        public async Task<IEnumerable<SignalRMessageAction>> RunProduction(
            [EventHubTrigger("%IOTHUB_EVENTHUB_NAME%",
                Connection = "IoTHubConnectionString",
                ConsumerGroup = "$Default",
                IsBatched = true)] IReadOnlyList<string> messages,
            FunctionContext context)
        {
            _logger.LogInformation("PROD: IoT Hub batch received {Count} messages", messages.Count);
            return await ProcessMessagesAsync(messages);
        }

        // Shared logic — used by both triggers
        private async Task<List<SignalRMessageAction>> ProcessMessagesAsync(IReadOnlyList<string> messages)
        {
            var actions = new List<SignalRMessageAction>();

            foreach (var message in messages)
            {
                if (string.IsNullOrWhiteSpace(message))
                {
                    _logger.LogWarning("Empty message received");
                    continue;
                }

                try
                {
                    var payload = JsonSerializer.Deserialize<TelemetryPayload>(message, new JsonSerializerOptions
                    {
                        PropertyNameCaseInsensitive = true
                    });

                    if (payload == null)
                    {
                        _logger.LogWarning("Failed to deserialize: payload is null");
                        continue;
                    }

                    if (string.IsNullOrWhiteSpace(payload.DeviceId))
                    {
                        _logger.LogWarning("Message missing DeviceId: {Message}", message);
                        continue;
                    }

                    payload.Timestamp = payload.Timestamp == default ? DateTime.UtcNow : payload.Timestamp;

                    await _storage.InsertTimeseriesAsync(payload);
                    await _storage.UpsertLatestAsync(payload);

                    _logger.LogInformation("Stored telemetry for device {DeviceId} at {Timestamp}",
                        payload.DeviceId, payload.Timestamp);

                    // Broadcast to SignalR group for this device
                    actions.Add(new SignalRMessageAction("telemetryReceived")
                    {
                        GroupName = $"device:{payload.DeviceId}",
                        Arguments = new[] { payload }
                    });
                }
                catch (JsonException jex)
                {
                    _logger.LogWarning(jex, "Invalid JSON received: {Message}", message);
                }
                catch (Exception ex)
                {
                    _logger.LogError(ex, "Error processing message");
                }
            }

            return actions;
        }
    }
}