# Lugdunum eserver 17.x (closed-source i686 Linux binary) under qemu-i386.
# natnet.py copies the binary named in natnet.env into the build context as ./eserver.
FROM debian:trixie-slim
RUN apt-get update && apt-get install -y --no-install-recommends qemu-user-static \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /eserver
COPY eserver /eserver/eserver
COPY entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod 755 /eserver/eserver /usr/local/bin/entrypoint.sh
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
