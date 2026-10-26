# The server image of a release: the stock vmangos-deploy server image with
# the bot data files of the same commit (bots/data: gear templates, spell
# files, route files). mangosd runs in /home/vmangos, so the default
# `Bots.DataDir = "bots"` reads /home/vmangos/bots. `release-core build`
# builds it with a context that holds only bots/data of the commit.
ARG BASE_IMAGE
FROM ${BASE_IMAGE}
COPY bots/data/ /home/vmangos/bots/
