# Play realm: one-time setup for the chat service

Do these steps by hand, once, before the first `scripts/release up` of the
chat service (the botchat repository). The release scripts change only the
image lines of the play realm's `compose.yaml`. They never add services or
settings.

The play realm is the vmangos-deploy folder, by default
`C:\Games\vmangos-deploy` (`/mnt/c/Games/vmangos-deploy` in WSL). Run its
`docker compose` commands from Windows, as always.

## 1. Choose the password of the `botchat` database user

Make a long random password, for example with `openssl rand -hex 24` in WSL.
Steps 3 and 4 use the same value. Call it `<botchat password>` below.

## 2. Copy `botchat.sql`

Copy `custom-sql/botchat.sql` of the botchat repository to
`storage/database/custom-sql/botchat.sql` in the play realm.

The `database` service runs every file in that folder as root on each start
(`VMANGOS_PROCESS_CUSTOM_SQL=1`, which the play realm already sets). The file
makes the `botchat` database and the `botchat` user, with rights on that
database only. It is safe to run again. Without `BOTCHAT_DB_PASSWORD` (step 3),
it stops with an error and changes nothing.

## 3. Set the password on the `database` service

In `compose.yaml`, add this line to `environment:` of the `database` service:

```yaml
      - BOTCHAT_DB_PASSWORD=<botchat password>
```

## 4. Add the `botchat` service

Add this service to `compose.yaml`, under `services:`. The first
`scripts/release up <commit>` replaces the image line with the released image
and adds `pull_policy: never`.

```yaml
  botchat:
    image: wowai-botchat:<commit>
    depends_on:
      database:
        condition: service_healthy
    restart: unless-stopped
    healthcheck:
      test: ['CMD', 'python', '-c', 'import urllib.request; urllib.request.urlopen("http://127.0.0.1:8000/health", timeout=2)']
      interval: 10s
      timeout: 3s
      retries: 3
      start_period: 30s
    extra_hosts:
      - host.docker.internal:host-gateway
    environment:
      - TZ=<the TZ of the other services>
      - BOTCHAT_LLM=llama
      - BOTCHAT_DB_PASSWORD=<botchat password>
```

The service calls `llama-server` on the Windows host at
`http://host.docker.internal:8080`. `BOTCHAT_LLAMA_URL` and
`BOTCHAT_LLAMA_MODEL` change the URL and the model name (see the README of
the botchat repository). The core reaches the service at
`http://botchat:8000` on the realm network (`BotChat.Url` in `mangosd.conf`).

## 5. Back up the `botchat` database

In `environment:` of the `database-backup` service, add `botchat` to
`DB_DUMP_INCLUDE`:

```yaml
      - DB_DUMP_INCLUDE=characters,realmd,maintenance,botchat
```

The daily backup and the backup-now command of the release scripts then
include the personas and the bot memory.

## 6. Start it

Run `docker compose up -d database database-backup` in the play realm. The
`database` service restarts and runs `botchat.sql`. Check its log for errors of
`botchat.sql`. Then release the chat service with `scripts/release build
<commit>` and `scripts/release up <commit>` in the botchat repository.
