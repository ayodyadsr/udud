# Tiny, portable image: static musl build copied into scratch.
#   docker build -t xcull .
#   cat urls.txt | docker run -i --rm xcull
# Note: -march=native is intentionally dropped here so the image runs on
# any x86-64/arm64 host, not just the machine that built it.
FROM alpine:3.20 AS build
RUN apk add --no-cache gcc musl-dev make
WORKDIR /src
COPY . .
RUN make CFLAGS="-O2 -flto -static -Wall -Wno-misleading-indentation" LDFLAGS="-static" \
    && ./xcull --version | head -n1

FROM scratch
COPY --from=build /src/xcull /xcull
ENTRYPOINT ["/xcull"]
