#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <zephyr/toolchain.h>
#include <zephyr/sys/util_macro.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/socket_poll.h>

LOG_MODULE_REGISTER(multinode);

#define LINK_UP_TIMEOUT 10000
#define LINK_UP_CHECK   100
#define PING_COUNT      4

#define MAGIC       "MNODE"
#define MSG_HELLO   MAGIC "Hello"
#define MSG_PING    MAGIC "Ping"
#define MSG_PONG    MAGIC "Pong"
#define MSG_GOODBYE MAGIC "Bye"

#define MSG_OFFSET(msg) ((msg) + sizeof(MAGIC) - 1)

K_TIMER_DEFINE(wait_link_up_timer, NULL, NULL);

K_SEM_DEFINE(multinode_mode, 1, 1);

K_EVENT_DEFINE(server_evt);
#define SERVER_EVT_START    BIT(0)
#define SERVER_EVT_RUNNING  BIT(1)
#define SERVER_EVT_STOP     BIT(2)
#define SERVER_EVT_SHUTDOWN BIT(3)

static char recv_buf[CONFIG_DEMO_MULTINODE_RX_BUF_SIZE];
static char send_buf[CONFIG_DEMO_MULTINODE_TX_BUF_SIZE];
static char ip_buf[NET_IPV4_ADDR_LEN];
static int sock;

static int wait_link_up(const struct shell *sh, struct net_if *iface)
{
	int steps = 0;

	(void)net_if_up(iface);

	if (net_if_is_up(iface)) {
		return 0;
	}

	shell_fprintf_normal(sh, "Waiting for link up");

	k_timer_start(&wait_link_up_timer, K_MSEC(LINK_UP_TIMEOUT), K_NO_WAIT);
	while (!net_if_is_up(iface)) {
		if (k_timer_status_get(&wait_link_up_timer) > 0) {
			shell_print(sh, " Timeout!");
			return -ETIMEDOUT;
		}

		k_sleep(K_MSEC(LINK_UP_CHECK));
		if (++steps == DIV_ROUND_UP(1000, LINK_UP_CHECK)) {
			shell_fprintf_normal(sh, ".");
			steps = 0;
		}
	}

	shell_print(sh, " done");
	return 0;
}

static int iface_addr_set(const struct shell *sh, const char *ip_str)
{
	struct net_if *iface = net_if_get_default();
	struct net_if *found = nullptr;
	struct in_addr addr, netmask;
	int rc;

	if (iface == nullptr) {
		return -ENODEV;
	}

	rc = wait_link_up(sh, iface);
	if (rc) {
		return rc;
	}

	if (zsock_inet_pton(AF_INET, ip_str, &addr) != 1) {
		return -EINVAL;
	}

	if (net_if_ipv4_addr_lookup(&addr, &found) == nullptr) {
		if (net_if_ipv4_addr_add(iface, &addr, NET_ADDR_MANUAL, 0) == nullptr) {
			return -EADDRNOTAVAIL;
		}
	}

	if (zsock_inet_pton(AF_INET, CONFIG_DEMO_MULTINODE_NETMASK, &netmask) != 1) {
		return -EINVAL;
	}

	if (!net_if_ipv4_set_netmask_by_addr(iface, &addr, &netmask)) {
		return -EADDRNOTAVAIL;
	}

	return 0;
}

static void iface_addr_rm(const char *ip_str)
{
	struct net_if *iface;
	struct in_addr addr;

	if (zsock_inet_pton(AF_INET, ip_str, &addr) != 1) {
		return;
	}

	if (net_if_ipv4_addr_lookup(&addr, &iface) != nullptr) {
		net_if_ipv4_addr_rm(iface, &addr);
	}
}

static int client_send(int sock, const struct sockaddr_in *const dst, const char *data,
		       size_t data_len, char *recv_buf, size_t recv_size)
{
	struct zsock_pollfd pfd = {.fd = sock, .events = ZSOCK_POLLIN};

	if (zsock_sendto(sock, send_buf, data_len, 0, (struct sockaddr *)dst, sizeof(*dst)) < 0) {
		return -errno;
	}

	if (zsock_poll(&pfd, 1, CONFIG_DEMO_MULTINODE_RX_TIMEOUT) <= 0) {
		return -ETIMEDOUT;
	}

	data_len = zsock_recv(sock, recv_buf, recv_size, 0);
	if (data_len <= 0) {
		return -EIO;
	}
	recv_buf[data_len] = 0;

	return data_len;
}

static void server_worker(void *a1, void *a2, void *a3)
{
	ARG_UNUSED(a1);
	ARG_UNUSED(a2);
	ARG_UNUSED(a3);

	struct sockaddr_in bind_addr = {.sin_family = AF_INET,
					.sin_addr.s_addr = htonl(INADDR_ANY),
					.sin_port = htons(CONFIG_DEMO_MULTINODE_SERVER_PORT)};

	/* Start in shutdown */
	k_event_set(&server_evt, SERVER_EVT_SHUTDOWN);

	for (;;) {
		k_event_wait(&server_evt, SERVER_EVT_START, false, K_FOREVER);

		sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (sock < 0) {
			LOG_ERR("socket creation failed (err=%d)", errno);
			k_event_set(&server_evt, SERVER_EVT_SHUTDOWN);
			continue;
		}

		if (zsock_bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
			LOG_ERR("binding failed (err=%d)", errno);
			k_event_set(&server_evt, SERVER_EVT_SHUTDOWN);
			continue;
		}

		k_event_set(&server_evt, SERVER_EVT_RUNNING);

		do {
			struct sockaddr_in peer = {0};
			socklen_t peer_len = sizeof(peer);
			struct zsock_pollfd pfd = {
				.fd = sock,
				.events = ZSOCK_POLLIN,
			};
			size_t msg_len;

			if (zsock_poll(&pfd, 1, 500) <= 0) {
				continue;
			}

			msg_len = zsock_recvfrom(sock, recv_buf, sizeof(recv_buf) - 1, 0,
						 (struct sockaddr *)&peer, &peer_len);
			if (msg_len <= 0) {
				continue;
			}

			recv_buf[msg_len] = 0;
			LOG_INF("incoming '%s'", MSG_OFFSET(recv_buf));

			zsock_inet_ntop(AF_INET, &peer.sin_addr, ip_buf, sizeof(ip_buf));

			size_t len = 0;
			if (strncmp(recv_buf, MSG_HELLO, strlen(MSG_HELLO)) == 0) {
				len = snprintf(send_buf, sizeof(send_buf), "%s %s", MSG_HELLO,
					       CONFIG_DEMO_MULTINODE_SERVER_IP);
			} else if (strncmp(recv_buf, MSG_PING, strlen(MSG_PING)) == 0) {
				len = snprintf(send_buf, sizeof(send_buf), "%s%s", MSG_PONG,
					       MSG_OFFSET(recv_buf) + 4);
			} else if (strncmp(recv_buf, MSG_GOODBYE, strlen(MSG_GOODBYE)) == 0) {
				len = snprintf(send_buf, sizeof(send_buf), "%s %s", MSG_GOODBYE,
					       CONFIG_DEMO_MULTINODE_SERVER_IP);
			} else {
				LOG_WRN("unknown message format, ignoring");
				continue;
			}
			zsock_sendto(sock, send_buf, len, 0, (struct sockaddr *)&peer, peer_len);
			LOG_INF("response '%s'", MSG_OFFSET(send_buf));

		} while (!k_event_test(&server_evt, SERVER_EVT_STOP));

		zsock_close(sock);
		k_event_set(&server_evt, SERVER_EVT_SHUTDOWN);
	}
}

K_THREAD_DEFINE(server_thrd, CONFIG_DEMO_MULTINODE_SERVER_THRD_STACK_SIZE, server_worker, nullptr,
		nullptr, nullptr, CONFIG_DEMO_MULTINODE_SERVER_THRD_PRIO, 0, 0);

static int multinode_cmd_server_start(const struct shell *sh, size_t argc, char *argv[]);
static int multinode_cmd_server_stop(const struct shell *sh, size_t argc, char *argv[]);
static int multinode_cmd_client(const struct shell *sh, size_t argc, char *argv[]);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_multinode_server_cmds,
			       SHELL_CMD_ARG(start, nullptr, "Start multinode server",
					     multinode_cmd_server_start, 1, 0),
			       SHELL_CMD_ARG(stop, nullptr, "Stop multinode server",
					     multinode_cmd_server_stop, 1, 0),
			       SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_multinode_cmds,
			       SHELL_CMD_ARG(server, &sub_multinode_server_cmds, "Server mode",
					     nullptr, 2, 0),
			       SHELL_CMD_ARG(client, nullptr, "Run in client mode",
					     multinode_cmd_client, 1, 0),
			       SHELL_SUBCMD_SET_END);

static int multinode_cmd_server_start(const struct shell *sh, size_t argc, char *argv[])
{
	int evt = 0;
	int rc;

	if (k_event_test(&server_evt, SERVER_EVT_START | SERVER_EVT_RUNNING)) {
		shell_error(sh, "Server already running");
		rc = -EINVAL;
		goto _server_cmd_mode_give;
	}

	if (k_sem_take(&multinode_mode, K_NO_WAIT)) {
		shell_error(sh, "Can't run in server and client mode at the same time");
		rc = -ENOTSUP;
		goto _server_cmd_mode_give;
	}

	rc = iface_addr_set(sh, CONFIG_DEMO_MULTINODE_SERVER_IP);
	if (rc) {
		shell_error(sh, "Failed to setup network interface (err=%d)", rc);
		goto _server_cmd_mode_give;
	}

	k_event_set(&server_evt, SERVER_EVT_START);

	evt = k_event_wait(&server_evt, SERVER_EVT_RUNNING | SERVER_EVT_SHUTDOWN, false, K_FOREVER);
	if (IS_BIT_SET(evt, SERVER_EVT_SHUTDOWN)) {
		shell_error(sh, "Failed to start server");
		rc = -ENODEV;
		goto _server_cmd_mode_give;
	}

	shell_print(sh, "Server started on %s:%d", CONFIG_DEMO_MULTINODE_SERVER_IP,
		    CONFIG_DEMO_MULTINODE_SERVER_PORT);

	return 0;

_server_cmd_mode_give:
	k_sem_give(&multinode_mode);

	return rc;
}

static int multinode_cmd_server_stop(const struct shell *sh, size_t argc, char *argv[])
{
	if (k_event_test(&server_evt, SERVER_EVT_STOP | SERVER_EVT_SHUTDOWN)) {
		shell_error(sh, "Server not running");
		return -EINVAL;
	}

	k_event_set(&server_evt, SERVER_EVT_STOP);

	shell_print(sh, "Stopping multinode server...");

	k_event_wait(&server_evt, SERVER_EVT_SHUTDOWN, false, K_FOREVER);
	iface_addr_rm(CONFIG_DEMO_MULTINODE_SERVER_IP);

	k_sem_give(&multinode_mode);
	shell_print(sh, "Done");

	return 0;
}

static int multinode_cmd_client(const struct shell *sh, size_t argc, char *argv[])
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int rc = 0;
	size_t len;

	if (k_sem_take(&multinode_mode, K_NO_WAIT)) {
		shell_error(sh, "Can't run in server and client mode at the same time");
		return -ENOTSUP;
	}

	rc = iface_addr_set(sh, CONFIG_DEMO_MULTINODE_CLIENT_IP);
	if (rc) {
		shell_error(sh, "Failed to setup network interface (err=%d)", rc);
		goto _cmd_client_mode_give;
	}

	sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
		rc = -errno;
		shell_error(sh, "Socket creation failed (err=%d)", rc);
		goto _cmd_client_addr_rm;
	}

	struct sockaddr_in dst = {.sin_family = AF_INET,
				  .sin_port = htons(CONFIG_DEMO_MULTINODE_SERVER_PORT)};
	if (zsock_inet_pton(AF_INET, CONFIG_DEMO_MULTINODE_SERVER_IP, &dst.sin_addr) != 1) {
		rc = -EINVAL;
		shell_error(sh, "Failed to parse server ip %s", CONFIG_DEMO_MULTINODE_SERVER_IP);
		goto _cmd_client_sock_close;
	};

	len = snprintf(send_buf, sizeof(send_buf), "%s %s", MSG_HELLO,
		       CONFIG_DEMO_MULTINODE_CLIENT_IP);

	shell_print(sh, "send '%s'", MSG_OFFSET(send_buf));
	rc = client_send(sock, &dst, send_buf, len, recv_buf, sizeof(recv_buf) - 1);
	if (rc < 0) {
		shell_error(sh, "Sending message failed (err=%d)", rc);
		goto _cmd_client_sock_close;
	}
	shell_print(sh, "resp '%s'", MSG_OFFSET(recv_buf));

	for (int i = 0; i <= PING_COUNT; ++i) {
		len = snprintf(send_buf, sizeof(send_buf), "%s%d", MSG_PING, i);
		int64_t now = k_uptime_get();

		shell_print(sh, "send '%s'", MSG_OFFSET(send_buf));
		rc = client_send(sock, &dst, send_buf, len, recv_buf, sizeof(recv_buf) - 1);
		if (rc < 0) {
			shell_error(sh, "Sending message failed (err=%d)", rc);
			goto _cmd_client_sock_close;
		}
		shell_print(sh, "resp '%s' (elapsed %" PRIi64 "ms)", MSG_OFFSET(recv_buf),
			    k_uptime_delta(&now));
		k_sleep(K_SECONDS(1));
	}

	len = snprintf(send_buf, sizeof(send_buf), "%s %s", MSG_GOODBYE,
		       CONFIG_DEMO_MULTINODE_CLIENT_IP);

	shell_print(sh, "send '%s'", MSG_OFFSET(send_buf));
	rc = client_send(sock, &dst, send_buf, len, recv_buf, sizeof(recv_buf) - 1);
	if (rc < 0) {
		shell_error(sh, "Sending message failed (err=%d)", rc);
		goto _cmd_client_sock_close;
	}
	shell_print(sh, "resp '%s'", MSG_OFFSET(recv_buf));

_cmd_client_sock_close:
	zsock_close(sock);

_cmd_client_addr_rm:
	iface_addr_rm(CONFIG_DEMO_MULTINODE_CLIENT_IP);

_cmd_client_mode_give:
	k_sem_give(&multinode_mode);

	return rc;
}

SHELL_CMD_REGISTER(multinode, &sub_multinode_cmds, "Multinode demo commands", NULL);
