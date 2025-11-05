using System.Threading.Tasks;
using Backend.Lib.Storage;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.DependencyInjection;

var host = new HostBuilder()
    .ConfigureFunctionsWorkerDefaults()
    .ConfigureServices((context, services) =>
    {
        var config = context.Configuration;
        var cosmosConn = config["CosmosConnectionString"];
        if (!string.IsNullOrWhiteSpace(cosmosConn))
        {
            services.AddSingleton<IStorage>(sp => new CosmosStorage(cosmosConn));
        }
        else
        {
            services.AddSingleton<IStorage>(sp => new FileStorage());
        }
    })
    .Build();

await host.RunAsync();
