use maho_agent::agent_tools::AgentToolsServer;

fn main() -> std::io::Result<()> {
    let server = AgentToolsServer::from_env();
    let stdin = std::io::stdin();
    let stdout = std::io::stdout();
    server.serve(stdin.lock(), stdout.lock())
}
