//
// Aspia Project
// Copyright (C) 2020 Dmitry Chapyshev <dmitry@aspia.ru>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//

#include "host/server.h"

#include "base/logging.h"
#include "base/task_runner.h"
#include "base/files/base_paths.h"
#include "base/files/file_path_watcher.h"
#include "base/net/network_channel.h"
#include "base/net/firewall_manager.h"
#include "host/client_session.h"

namespace host {

namespace {

const wchar_t kFirewallRuleName[] = L"Aspia Host Service";
const wchar_t kFirewallRuleDecription[] = L"Allow incoming TCP connections";

} // namespace

Server::Server(std::shared_ptr<base::TaskRunner> task_runner)
    : task_runner_(std::move(task_runner))
{
    DCHECK(task_runner_);
}

Server::~Server()
{
    LOG(LS_INFO) << "Stopping the server...";

    settings_watcher_.reset();
    authenticator_manager_.reset();
    user_session_manager_.reset();
    server_.reset();

    deleteFirewallRules();

    LOG(LS_INFO) << "Server is stopped";
}

void Server::start()
{
    if (server_)
    {
        DLOG(LS_WARNING) << "An attempt was start an already running server";
        return;
    }

    LOG(LS_INFO) << "Starting the host server";

    settings_watcher_ = std::make_unique<base::FilePathWatcher>(task_runner_);
    settings_watcher_->watch(settings_.filePath(), false,
        std::bind(&Server::updateConfiguration, this, std::placeholders::_1, std::placeholders::_2));

    authenticator_manager_ =
        std::make_unique<base::ServerAuthenticatorManager>(task_runner_, this);

    user_session_manager_ = std::make_unique<UserSessionManager>(task_runner_);
    user_session_manager_->start(this);

    reloadUserList();
    addFirewallRules();

    server_ = std::make_unique<base::NetworkServer>();
    server_->start(settings_.tcpPort(), this);

    if (settings_.isRouterEnabled())
        connectToRouter();

    LOG(LS_INFO) << "Host server is started successfully";
}

void Server::setSessionEvent(base::win::SessionStatus status, base::SessionId session_id)
{
    if (user_session_manager_)
        user_session_manager_->setSessionEvent(status, session_id);
}

void Server::onNewConnection(std::unique_ptr<base::NetworkChannel> channel)
{
    LOG(LS_INFO) << "New DIRECT connection";
    startAuthentication(std::move(channel));
}

void Server::onRouterStateChanged(const proto::internal::RouterState& router_state)
{
    user_session_manager_->setRouterState(router_state);
}

void Server::onHostIdAssigned(base::HostId host_id, const base::ByteArray& host_key)
{
    LOG(LS_INFO) << "New host ID assigned: " << host_id;

    if (!host_key.empty())
    {
        LOG(LS_INFO) << "Host key changed";
        settings_.setHostKey(host_key);
    }

    user_session_manager_->setHostId(host_id);
}

void Server::onClientConnected(std::unique_ptr<base::NetworkChannel> channel)
{
    LOG(LS_INFO) << "New RELAY connection";
    startAuthentication(std::move(channel));
}

void Server::onNewSession(base::ServerAuthenticatorManager::SessionInfo&& session_info)
{
    std::unique_ptr<ClientSession> session = ClientSession::create(
        static_cast<proto::SessionType>(session_info.session_type), std::move(session_info.channel));

    if (session)
    {
        session->setVersion(session_info.version);
        session->setUserName(session_info.user_name);
    }

    if (user_session_manager_)
        user_session_manager_->addNewSession(std::move(session));
}

void Server::onUserListChanged()
{
    reloadUserList();
}

void Server::startAuthentication(std::unique_ptr<base::NetworkChannel> channel)
{
    static const size_t kReadBufferSize = 1 * 1024 * 1024; // 1 Mb.

    channel->setReadBufferSize(kReadBufferSize);
    channel->setNoDelay(true);

    if (authenticator_manager_)
        authenticator_manager_->addNewChannel(std::move(channel));
}

void Server::addFirewallRules()
{
    std::filesystem::path file_path;
    if (!base::BasePaths::currentExecFile(&file_path))
        return;

    base::FirewallManager firewall(file_path);
    if (!firewall.isValid())
        return;

    if (!firewall.addTcpRule(kFirewallRuleName, kFirewallRuleDecription, settings_.tcpPort()))
        return;

    LOG(LS_INFO) << "Rule is added to the firewall";
}

void Server::deleteFirewallRules()
{
    std::filesystem::path file_path;
    if (!base::BasePaths::currentExecFile(&file_path))
        return;

    base::FirewallManager firewall(file_path);
    if (!firewall.isValid())
        return;

    firewall.deleteRuleByName(kFirewallRuleName);
}

void Server::updateConfiguration(const std::filesystem::path& path, bool error)
{
    LOG(LS_INFO) << "Configuration file change detected";

    if (!error)
    {
        DCHECK_EQ(path, settings_.filePath());

        // Synchronize the parameters from the file.
        settings_.sync();

        // Reload user lists.
        reloadUserList();

        // If a controller instance already exists.
        if (router_controller_)
        {
            if (settings_.isRouterEnabled())
            {
                // Check if the connection parameters have changed.
                if (router_controller_->address() != settings_.routerAddress() ||
                    router_controller_->port() != settings_.routerPort() ||
                    router_controller_->publicKey() != settings_.routerPublicKey())
                {
                    // Reconnect to the router with new parameters.
                    LOG(LS_INFO) << "Router parameters have changed";
                    connectToRouter();
                }
            }
            else
            {
                // Destroy the controller.
                LOG(LS_INFO) << "The router is now disabled";
                router_controller_.reset();

                proto::internal::RouterState router_state;
                router_state.set_state(proto::internal::RouterState::DISABLED);
                user_session_manager_->setRouterState(router_state);
            }
        }
        else
        {
            if (settings_.isRouterEnabled())
                connectToRouter();
        }
    }
}

void Server::reloadUserList()
{
    // Read the list of regular users.
    std::unique_ptr<base::UserList> user_list(settings_.userList().release());

    // Add a list of one-time users to the list of regular users.
    user_list->merge(*user_session_manager_->userList());

    // Updating the list of users.
    authenticator_manager_->setUserList(std::move(user_list));
}

void Server::connectToRouter()
{
    LOG(LS_INFO) << "Connecting to the router...";

    // Destroy the previous instance.
    router_controller_.reset();

    // Fill the connection parameters.
    RouterController::RouterInfo router_info;
    router_info.address = settings_.routerAddress();
    router_info.port = settings_.routerPort();
    router_info.public_key = settings_.routerPublicKey();
    router_info.host_key = settings_.hostKey();

    // Connect to the router.
    router_controller_ = std::make_unique<RouterController>(task_runner_);
    router_controller_->start(router_info, this);
}

} // namespace host
