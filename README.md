PoC backend for ESP32 moisture mesh -> Azure pipeline.

Structure:
- src/Backend.Lib - DTOs and file-based storage for local PoC
- src/Backend.Functions - Azure Function (IoT Hub trigger) that stores telemetry
- src/Backend.Api - minimal API to query devices & timeseries

This PoC uses file storage under `data/` for easy local testing. Replace `FileStorage` with Cosmos/Blob-backed implementation for production.

Next steps:
- Implement SignalR broadcasting in Function
- Replace FileStorage with Cosmos DB & Blob Storage
- Add CI/CD workflows
