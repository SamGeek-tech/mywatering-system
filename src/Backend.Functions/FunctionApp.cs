using Microsoft.Azure.Functions.Worker.Extensions.OpenApi.Extensions;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Backend.Lib.Storage;

var host = new HostBuilder()
    .ConfigureFunctionsWorkerDefaults(builder =>
    {
        // configure openapi or other extensions if needed
    })
    .ConfigureServices((context, services) =>
    {
        var cfg = context.Configuration;
        var cosmos = cfg["CosmosConnectionString"];
        if (!string.IsNullOrWhiteSpace(cosmos))
        {
            services.AddSingleton<IStorage>(sp => new CosmosStorage(cosmos));
        }
        else
        {
            services.AddSingleton<IStorage>(sp => new FileStorage());
        }
    })
    .Build();

await host.RunAsync();