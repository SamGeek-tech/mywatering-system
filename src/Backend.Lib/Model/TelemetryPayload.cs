using System;
using System.Collections.Generic;
using System.Text.Json.Serialization;

namespace Backend.Lib.Model
{
    public class TelemetryPayload
    {
        [JsonPropertyName("deviceId")]
        public string DeviceId { get; set; } = string.Empty;

        [JsonPropertyName("timestamp")]
        public DateTime Timestamp { get; set; }

        [JsonPropertyName("sensors")]
        public List<SensorReading> Sensors { get; set; } = new List<SensorReading>();

        [JsonPropertyName("battery")]
        public double? Battery { get; set; }

        [JsonPropertyName("rssi")]
        public int? Rssi { get; set; }

        [JsonPropertyName("meshHopCount")]
        public int? MeshHopCount { get; set; }

        [JsonPropertyName("firmwareVersion")]
        public string? FirmwareVersion { get; set; }
    }

    public class SensorReading
    {
        [JsonPropertyName("name")]
        public string Name { get; set; } = string.Empty;

        [JsonPropertyName("type")]
        public string? Type { get; set; }

        [JsonPropertyName("value")]
        public double Value { get; set; }

        [JsonPropertyName("unit")]
        public string? Unit { get; set; }
    }
}