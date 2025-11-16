using Backend.Lib.Storage;
using Microsoft.Azure.Functions.Worker;
using Microsoft.Azure.Functions.Worker.Builder;
using Microsoft.Azure.Functions.Worker.ApplicationInsights;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;

var host = new HostBuilder()
    .ConfigureFunctionsWebApplication() // replaces ConfigureFunctionsWorkerDefaults
    .ConfigureServices((context, services) =>
    {
        // ✅ Correct: Application Insights is registered here
        services.AddApplicationInsightsTelemetryWorkerService();
        services.ConfigureFunctionsApplicationInsights();

        var config = context.Configuration;
        var cosmosConn = config["CosmosConnectionString"];

        if (!string.IsNullOrWhiteSpace(cosmosConn))
        {
            services.AddSingleton<IStorage>(_ => new CosmosStorage(cosmosConn));
        }
        else
        {
            services.AddSingleton<IStorage, FileStorage>();
        }
    })
    .Build();

await host.RunAsync();
