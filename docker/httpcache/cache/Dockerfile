# ---------------------------------------------------------------------------
# Dockerfile — the cache server the swarm publishes to: nginx + php-fpm running
# the reference backend from https://github.com/ModderMule/emule-http-cache-php
#
# Built by cachenet.py as emuleqt-httpcache:latest. Pass --cache-src to
# cachenet.py to bind-mount a local checkout over /app instead, when the backend
# itself is what you are changing.
# ---------------------------------------------------------------------------
FROM --platform=linux/amd64 php:8.3-fpm-alpine

ARG CACHE_REPO=https://github.com/ModderMule/emule-http-cache-php
ARG CACHE_REF=main

RUN apk add --no-cache nginx git

# The app needs no Composer and no PHP extension beyond the defaults; ext-curl
# and ext-openssl are only used by its own test suite, which is handy to have
# here for probing the server from inside the container.
RUN git clone --depth 1 --branch "${CACHE_REF}" "${CACHE_REPO}" /app \
    && rm -rf /app/.git

# php-fpm logs a request line of its own, which would double every entry in the
# stream the analyzer reads — nginx is the one with the original URI.
RUN printf '[www]\naccess.log = /dev/null\n' > /usr/local/etc/php-fpm.d/zzz-rig.conf

COPY docker/httpcache/cache/nginx.conf /etc/nginx/nginx.conf
COPY docker/httpcache/cache/php.ini /usr/local/etc/php/conf.d/zz-httpcache.ini
COPY docker/httpcache/cache/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

EXPOSE 80
ENTRYPOINT ["/entrypoint.sh"]
