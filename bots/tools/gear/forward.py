"""A TCP forwarder: listen on 0.0.0.0:<port> and connect each client to
<host>:<port>. generate-from-realm runs it in a container on the network of a
test realm, so that the generator on the host reaches its database. Standard
library only."""

import asyncio
import sys


async def pipe(reader, writer):
    try:
        while data := await reader.read(65536):
            writer.write(data)
            await writer.drain()
    finally:
        writer.close()


async def main(host, port):
    async def handle(client_reader, client_writer):
        server_reader, server_writer = await asyncio.open_connection(host, port)
        await asyncio.gather(pipe(client_reader, server_writer), pipe(server_reader, client_writer),
                             return_exceptions=True)

    server = await asyncio.start_server(handle, "0.0.0.0", port)
    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    asyncio.run(main(sys.argv[1], int(sys.argv[2])))
