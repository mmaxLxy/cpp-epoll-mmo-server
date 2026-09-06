# MySQL、Redis 与异步数据访问

## 目标

网络事件循环不能等待数据库查询。一次查询即使只阻塞 50 毫秒，也会让同一事件循环中的其他连接在这段时间得不到处理。

本模块把工作分为两类线程：

```text
事件循环线程                    数据库工作线程
     |                               |
     |-- TrySubmit(请求) ----------->| 访问 MySQL / Redis
     |  继续处理网络事件              |
     |                               |-- Push(普通结果)
     |<--------- eventfd 可读 --------|
     |-- Drain() 执行业务回调          |
```

工作线程不持有 socket，不修改玩家和 AOI 对象。业务回调由事件循环线程执行，线程之间只传递拥有明确所有权的值对象。

## 模块职责

- `BoundedThreadPool`：固定数量的工作线程和有界任务队列；满载时 `TrySubmit` 立即失败。
- `CompletionQueue`：工作线程放入完成回调，所有者线程集中执行。
- `EventFdWakeup`：把“有数据库结果”转换为 Linux 可监听的文件描述符事件。
- `AsyncDataService`：组合线程池、完成队列和存储接口。
- `MySqlPlayerRepository`：通过预处理语句读写玩家长期数据。
- `RedisSessionStore`：通过 `SET ... EX` 保存带 TTL 的短期会话。

## 本地准备

创建一个独立测试库和最小权限账户。将示例中的密码替换为只用于本机测试的密码：

```sql
CREATE DATABASE mmo_test CHARACTER SET utf8mb4;
CREATE USER 'mmo_test'@'127.0.0.1' IDENTIFIED BY 'replace_me';
GRANT ALL PRIVILEGES ON mmo_test.* TO 'mmo_test'@'127.0.0.1';
```

构建并运行：

```bash
cmake -S . -B build \
  -DBUILD_TESTING=ON \
  -DMMO_ENABLE_DATABASES=ON
cmake --build build

export MMO_MYSQL_USER=mmo_test
export MMO_MYSQL_PASSWORD=replace_me
export MMO_MYSQL_DATABASE=mmo_test
ctest --test-dir build --output-on-failure
```

如果未设置 `MMO_MYSQL_USER` 或 `MMO_MYSQL_DATABASE`，真实数据库集成测试会跳过，其他不依赖数据库的测试仍会运行。

## epoll 中的实际调用顺序

1. 创建 `EventFdWakeup`。
2. 用调用 `wakeup.Notify()` 的函数构造 `CompletionQueue`。
3. 把 `wakeup.NativeHandle()` 注册到 epoll，关注可读事件。
4. 收到该事件后先调用 `wakeup.Consume()`，再调用 `completions.Drain()`。
5. 若 `TrySubmit` 返回 `false`，向客户端返回“服务繁忙”，不要在网络线程等待队列空位。

## 当前边界

- 线程池是数据库任务线程池，不是数据库连接池。
- MySQL 和 Redis 适配器目前每次操作建立独立连接，适合先验证正确性和线程隔离。
- 只有在端到端压测证明连接建立是主要瓶颈后，才需要增加每工作线程连接复用、重连与健康检查。
- 当前仓库已将该通知链路接入 `GameServer`，并通过真实 TCP + MySQL + Redis 的服务器集成测试。
