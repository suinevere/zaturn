# Hosting the MultiZork server (Docker)

This directory containerizes **`multizorkd`** — the multiplayer Zork telnet
server that **Play Online** connects to. The image is **self-contained**: it
clones the source from GitHub and builds it at image-build time, so the host
running it needs **no local checkout** — just Docker and these two files
(`Dockerfile`, `docker-compose.yml`).

Our live instance runs on an **Oracle Cloud Free Tier** VM and is reachable at
**`suinevere.duckdns.org`** (telnet port 23, with 2323 as an alternate).

---

## What the image does

- **Build stage** installs `git` + a C toolchain + `libsqlite3-dev`, clones
  `suinevere/zaturn` (branch `main`), and compiles `saturn/multizorkd.c`
  (which `#include`s `mojozork.c`) against system SQLite.
- **Runtime stage** ships only the `multizorkd` binary and `ZORK1.Z3`, runs as a
  **non-root** user, and serves telnet on container port **2323**.
- `multizork.sqlite3` (game instances + transcripts) is written to `/data`, kept
  on a named volume so it survives restarts.

> MultiZork is **Zork 1-specific** — `multizorkd.c` contains hardcoded Zork 1
> story-address patches — so `ZORK1.Z3` is the only story it can serve.

### Build args (override with `--build-arg`)

| Arg | Default | Purpose |
|---|---|---|
| `REPO_URL` | `https://github.com/suinevere/zaturn.git` | source to clone |
| `REPO_REF` | `main` | branch/tag to build |

A `docker compose build` picks up new commits automatically (a GitHub API
cache-bust invalidates the clone layer); force a clean rebuild with
`docker compose build --no-cache`.

---

## Quick start (any Docker host)

```bash
git clone https://github.com/suinevere/zaturn.git
cd zaturn/docker
docker compose up -d --build
docker compose logs            # expect: "Now accepting connections on port 2323"
telnet localhost 23            # "Hello sailor!" = working
```

`docker-compose.yml` publishes **host 23 and 2323** (both map to container 2323),
uses `restart: unless-stopped`, and persists state in a bind mount at
`/srv/multizork` (override with `MULTIZORK_DATA`). Some client ISPs block
outbound port 23, so 2323 gives those players a way in.

The mount is a host directory rather than a named volume because the transcripts
site runs under host nginx and has to read `multizork.sqlite3` out of it. Create
it owned by the container's unprivileged user before the first start:

```bash
sudo mkdir -p /srv/multizork
sudo chown 1000:1000 /srv/multizork
sudo chmod 755 /srv/multizork
```

> **Upgrading from the old named volume?** Data in `multizork-data` will not
> appear in the bind mount by itself. Copy it across once, with the stack down:
> ```bash
> docker compose down
> docker run --rm -v docker_multizork-data:/from -v /srv/multizork:/to \
>            debian:bookworm-slim cp -a /from/. /to/
> sudo chown -R 1000:1000 /srv/multizork
> docker compose up -d
> ```

---

## Production hosting: Oracle Cloud Free Tier + DuckDNS

How the live `suinevere.duckdns.org` instance is set up, end to end.

### 1. On the instance

```bash
curl -s ifconfig.me; echo                     # note the public IPv4

# deploy
sudo apt-get update && sudo apt-get install -y docker.io docker-compose-plugin git
git clone https://github.com/suinevere/zaturn.git
cd zaturn/docker
sudo docker compose up -d --build
```

Open the **host firewall** — Oracle images block everything but SSH by default.
Check the OS with `cat /etc/os-release`, then:

- **Ubuntu** (uses iptables, not ufw, by default):
  ```bash
  sudo iptables -I INPUT 6 -m state --state NEW -p tcp --dport 23   -j ACCEPT
  sudo iptables -I INPUT 6 -m state --state NEW -p tcp --dport 2323 -j ACCEPT
  sudo netfilter-persistent save
  ```
- **Oracle Linux / RHEL** (firewalld):
  ```bash
  sudo firewall-cmd --permanent --add-port=23/tcp --add-port=2323/tcp
  sudo firewall-cmd --reload
  ```

Confirm it listens locally before touching DNS: `telnet localhost 23`.

### 2. Oracle Cloud Console — open the ports at the network layer

The host firewall isn't enough; OCI's virtual network blocks inbound too.

1. Console **☰ → Networking → Virtual Cloud Networks** → your **VCN**.
2. **Resources → Security Lists** → the **Default Security List** for your subnet.
3. **Add Ingress Rules**, one per port (23 and 2323):
   - Stateless: **unchecked**
   - Source Type: **CIDR**, Source CIDR: `0.0.0.0/0`
   - IP Protocol: **TCP**
   - Source Port Range: *(blank)*
   - Destination Port Range: `23` (repeat for `2323`)
4. **Add Ingress Rules**.

(If your instance uses a Network Security Group instead, add the same rules under
**Networking → Network Security Groups → your NSG**.)

### 3. DuckDNS — free domain pointing at the instance

1. Go to **duckdns.org**, sign in (GitHub / Google / etc.).
2. Add a subdomain — ours is `suinevere` → `suinevere.duckdns.org`.
3. Paste the instance's **public IPv4** into the **current ip** box → **update ip**.
   (Static IP = set it once; no dynamic-DNS updater needed.)

### 4. Verify from another machine

```bash
nslookup suinevere.duckdns.org      # returns the Oracle IP
telnet   suinevere.duckdns.org 23   # "Hello sailor!"
```

DNS may take a few minutes the first time. If it resolves but telnet hangs, it's
always one of the two firewall layers (step 1 or 2).

---

## Serving `zaturn.netbin` over HTTP (nginx, for PlanetWeb 4.0)

`zaturn.netbin` (the online-only Saturn client `saturn/compile.bat` produces
alongside the CD image — see the top-level [README](../README.md)) is fetched by
the **PlanetWeb 4.0** Saturn web browser as a plain HTTP download. This is
separate infrastructure from the Docker `multizorkd` container above: it's a
system **nginx** installed directly on the same Oracle instance, serving one
static file at `/zork` and reverse-proxying everything else to a personal
GitHub Pages site.

### 1. Open the ports (Oracle Cloud Console)

Same two-layer story as the telnet ports above — see steps 1–2 there for the
Console navigation. Add ingress rules for **80** and **443** the same way:

- Stateless: **unchecked**
- Source Type: **CIDR**, Source CIDR: `0.0.0.0/0`
- IP Protocol: **TCP**
- Source Port Range: *(blank)*
- Destination Port Range: `80` (repeat for `443`)

### 2. Install nginx + Certbot, deploy the config

```bash
sudo apt-get update && sudo apt-get install -y nginx certbot python3-certbot-nginx
sudo mkdir -p /var/www/html
```

Copy [`nginx/suinevere.duckdns.org.conf`](nginx/suinevere.duckdns.org.conf) — the
live config, committed here for reference — into place:

```bash
sudo cp docker/nginx/suinevere.duckdns.org.conf /etc/nginx/sites-available/suinevere.duckdns.org
sudo ln -s /etc/nginx/sites-available/suinevere.duckdns.org /etc/nginx/sites-enabled/
sudo nginx -t && sudo systemctl reload nginx
```

The `listen 443 ssl` block and its `ssl_certificate*`/`options-ssl-nginx.conf`
lines are Certbot-managed — they don't exist until Certbot issues the
certificate the first time:

```bash
sudo certbot --nginx -d suinevere.duckdns.org
```

Certbot needs the port-80 server block reachable first (the
`/.well-known/acme-challenge/` location handles that), then rewrites this file
in place to add the `443` block and points the two `# managed by Certbot`
lines at the issued cert. Re-run `certbot renew --dry-run` periodically to
confirm auto-renewal still works.

### 3. Deploy a freshly built `zaturn.netbin`

Every `saturn/compile.bat` run produces a new `saturn/BuildDrop/zaturn.netbin`.
Get it onto the host (`scp`, a CI artifact download, whatever fits your
workflow) and drop it at the path nginx serves:

```bash
curl -O https://suinevere.duckdns.org/NETBIN/zaturn.netbin
sudo mv -f ./zaturn.netbin /var/www/html/zork
```

Note the destination filename is `zork`, not `zork.netbin` — nginx serves it
at exactly the path PlanetWeb requests, `/zork`, with no extension.

### 4. Verify

```bash
curl -sI https://suinevere.duckdns.org/zork | grep -i content-type
# expect: Content-Type: application/x-planetweb-app-segasaturn
```

On the Saturn, point PlanetWeb 4.0 at `https://suinevere.duckdns.org` and click
the **ZORK** link in the sidebar, or go straight to
`https://suinevere.duckdns.org/zork`. Either one downloads and launches
`zaturn.netbin`, which boots to the dialer and connects to the same multizork
server the NetLink telnet path (above) does.

> **The two server blocks disagree on how `/zork` is served.** The port-80
> block applies `default_type application/x-planetweb-app-segasaturn` and the
> `Content-Disposition: attachment; filename=zork` header unconditionally to
> the whole `/zork` location. The port-443 block instead nests that override
> inside `location ~* \.netbin$` — a regex that never matches, because the
> file is deployed as `/zork` with no `.netbin` suffix (see step 3). So over
> HTTPS, `/zork` currently falls through to nginx's default MIME/header
> handling with neither the PlanetWeb content-type nor the download header
> applied. In practice this hasn't mattered: PlanetWeb 4.0, a mid-2000s
> console browser, has no TLS stack, so it only ever reaches `/zork` over
> plain port 80, where the override does apply. Worth reconciling the two
> blocks if `/zork` is ever served to an HTTPS-capable client, but it isn't
> live-broken for the client that matters today.

---

## Serving the transcripts site (nginx + PHP-FPM)

The daemon hands every departing player a URL like
`https://suinevere.duckdns.org/game/<room>`. Three routes serve those, all from
the single file `saturn/multizork-transcripts.php`:

| Path | Renders |
| --- | --- |
| `/game/<room>` | The instance facts and links to each player's transcript |
| `/player/<room>/<id>` | That player's transcript as styled HTML |
| `/rawplayer/<room>/<id>` | The same transcript as plain text |

They need no extra firewall rules — all three are paths on the 443 listener you
already opened for `/zork`.

### 1. Install PHP and deploy the file

```bash
sudo apt-get install -y php-fpm php-sqlite3
sudo mkdir -p /var/www/multizork
sudo cp saturn/multizork-transcripts.php /var/www/multizork/transcripts.php
```

The database path defaults to `/srv/multizork/multizork.sqlite3` and can be
overridden per-vhost with the `MULTIZORK_DB` fastcgi param, which the shipped
config sets. PHP opens it read-only, and the daemon uses SQLite's default
rollback journal rather than WAL, so `www-data` needs no write access — only
read on the file and traverse on the directory, which the `755` above gives.

### 2. Check the socket path

The config points at `unix:/run/php/php8.2-fpm.sock`. Confirm what your host
actually runs and edit the `fastcgi_pass` line if it differs:

```bash
ls /run/php/
```

### 3. Provide the error pages

The site answers a failure with an HTTP status and no body on purpose, leaving
the page to nginx. `fastcgi_intercept_errors on` and the `error_page` lines are
in the config; supply the documents they name, or a bad URL renders blank:

```bash
echo 'Not found.' | sudo tee /var/www/html/404.html
echo 'Temporarily unavailable.' | sudo tee /var/www/html/503.html
```

### 4. Reload and verify

```bash
sudo nginx -t && sudo systemctl reload nginx
curl -sI https://suinevere.duckdns.org/game/does-not-exist   # expect 404
```

Then finish a real game and follow the link the daemon prints on the way out.

> **Why `SCRIPT_NAME` is left alone and `SCRIPT_FILENAME` is hardcoded.** The
> router reads `$_SERVER['PHP_SELF']` and splits it on `/`, so it needs
> `PHP_SELF` to be the bare request path. With no `fastcgi_split_path_info` in
> the location, `$fastcgi_script_name` is `$uri` and that is what arrives. Add a
> path-info split — as the stock `snippets/fastcgi-php.conf` does — and
> `PHP_SELF` becomes `/transcripts.php/game/<room>`, the router reads
> `transcripts.php` as the operation, and every transcript URL 404s. That is why
> the config spells the params out instead of including that snippet.

---

## Routing the Saturn NetLink dial code to this server (DreamPi)

**Play Online** dials NetLink into a **DreamPi** running the eaudunord Netlink
tunnel, which relays dial code `199403` to a telnet server. Point that code at
this deployment by editing the DreamPi's `/dreampi/netlink_config.ini`:

```ini
[server:199403]
name = MultiZork
host = suinevere.duckdns.org
port = 23
handler = transparent
```

`handler = transparent` is required — multizork does no AUTH handshake. See the
top-level [README](../README.md) section *Playing online from a real Saturn* for
the full DreamPi steps.

---

## Operations

```bash
docker compose logs -f                 # live server log
docker compose restart                 # restart
docker compose build --no-cache && docker compose up -d   # rebuild latest source
docker compose down                    # stop (keeps the data directory)
sudo rm -rf /srv/multizork/*           # wipe all game state + transcripts
```
