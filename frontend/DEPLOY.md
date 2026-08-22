# 前端最小 CI/CD 方案

这个前端目录使用 GitHub Actions 直接同步到 ECS，不引入额外构建步骤。

## 工作方式

1. 推送 `main` 分支时自动触发部署。
2. Actions 通过 SSH 连接 ECS。
3. 使用 `rsync` 将 `frontend/` 目录同步到 ECS 上的静态站点目录。
4. ECS 上的 Nginx 负责对外提供静态页面。

## 需要配置的 GitHub Secrets

- `ECS_HOST`：ECS 公网 IP 或域名
- `ECS_USER`：SSH 登录用户
- `ECS_SSH_KEY`：用于登录 ECS 的私钥内容
- `ECS_SSH_PORT`：SSH 端口，默认一般是 `22`
- `ECS_TARGET_DIR`：Nginx 静态目录，例如 `/var/www/rk3576-streamer`

## ECS 目录建议

建议把前端页面放到类似下面的目录：

```bash
/var/www/rk3576-streamer/
```

然后让 Nginx 的 `root` 指向这个目录即可。

## 本地预览

前端开发时，直接打开 `index.html` 也可以，但更推荐用本地静态服务器预览，避免后面接接口时出现路径问题。
