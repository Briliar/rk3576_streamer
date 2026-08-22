# 前端最小 CI/CD 方案

这个前端目录使用 GitHub Actions 直接同步到 ECS，不引入额外构建步骤。

## 工作方式

1. 推送 `main` 分支时自动触发部署。
2. Actions 通过 SSH 连接 ECS。
3. 使用 `rsync` 将 `frontend/` 目录同步到 ECS 上的 `/monitor/` 子目录。
4. ECS 上的 Nginx 负责对外提供静态页面。

## 需要配置的 GitHub Secrets

- `ECS_HOST`：ECS 公网 IP 或域名
- `ECS_USER`：SSH 登录用户
- `ECS_SSH_KEY`：用于登录 ECS 的**私钥内容**，不是公钥
- `ECS_SSH_PORT`：SSH 端口，默认一般是 `22`
- `ECS_TARGET_DIR`：Nginx 静态目录，例如 `/var/www/rk3576-streamer/monitor`

## SSH Key 常见错误

如果 GitHub Actions 报 `Error loading key \"(stdin)\": error in libcrypto`，通常是下面几种情况之一：

- 你把**公钥**填进了 `ECS_SSH_KEY`
- 私钥内容被复制坏了，少了开头或结尾的标记
- 私钥带了 Windows 换行符或额外空格
- 私钥被加密了，但 workflow 没有提供 passphrase

最稳的做法是重新生成一对专门给部署用的 SSH key，然后把**私钥**完整复制到 GitHub Secrets 里。

例如：

```bash
ssh-keygen -t ed25519 -f ~/.ssh/rk3576_github_actions -C "github-actions"
```

然后把 `~/.ssh/rk3576_github_actions.pub` 追加到 ECS 的 `~/.ssh/authorized_keys`，再把 `~/.ssh/rk3576_github_actions` 的内容放进 `ECS_SSH_KEY`。

## ECS 目录建议

建议把前端页面放到类似下面的目录：

```bash
/var/www/rk3576-streamer/monitor/
```

然后让 Nginx 的 `root` 指向这个目录即可。

## 如果访问还是 404

优先检查 ECS 上是否还有其他 `listen 80` 且 `server_name _` 的站点配置。

```bash
sudo nginx -T | grep -n "server_name _\|default_server\|listen 80"
```

如果 ECS 上自带了 `/etc/nginx/sites-enabled/default`，建议直接禁用它：

```bash
sudo unlink /etc/nginx/sites-enabled/default
```

然后再执行：

```bash
sudo nginx -t
sudo systemctl reload nginx
```

如果你不想删默认站点，那就不要让本配置使用 `default_server`，并且要把 `server_name` 改成 ECS 公网 IP 或域名。

## 本地预览

前端开发时，直接打开 `index.html` 也可以，但更推荐用本地静态服务器预览，避免后面接接口时出现路径问题。
