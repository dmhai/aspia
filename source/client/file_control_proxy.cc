//
// Aspia Project
// Copyright (C) 2019 Dmitry Chapyshev <dmitry@aspia.ru>
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

#include "client/file_control_proxy.h"

#include "base/logging.h"
#include "base/task_runner.h"

namespace client {

FileControlProxy::FileControlProxy(
    std::shared_ptr<base::TaskRunner> io_task_runner, FileControl* file_control)
    : io_task_runner_(io_task_runner),
      file_control_(file_control)
{
    DCHECK(io_task_runner_);
    DCHECK(io_task_runner_->belongsToCurrentThread());
    DCHECK(file_control_);
}

FileControlProxy::~FileControlProxy()
{
    DCHECK(!file_control_);
}

void FileControlProxy::dettach()
{
    DCHECK(io_task_runner_->belongsToCurrentThread());
    file_control_ = nullptr;
}

void FileControlProxy::getDriveList(common::FileTaskTarget target)
{
    if (!io_task_runner_->belongsToCurrentThread())
    {
        io_task_runner_->postTask(
            std::bind(&FileControlProxy::getDriveList, shared_from_this(), target));
        return;
    }

    if (file_control_)
        file_control_->getDriveList(target);
}

void FileControlProxy::getFileList(common::FileTaskTarget target, const std::string& path)
{
    if (!io_task_runner_->belongsToCurrentThread())
    {
        io_task_runner_->postTask(
            std::bind(&FileControlProxy::getFileList, shared_from_this(), target, path));
        return;
    }

    if (file_control_)
        file_control_->getFileList(target, path);
}

void FileControlProxy::createDirectory(common::FileTaskTarget target, const std::string& path)
{
    if (!io_task_runner_->belongsToCurrentThread())
    {
        io_task_runner_->postTask(
            std::bind(&FileControlProxy::createDirectory, shared_from_this(), target, path));
        return;
    }

    if (file_control_)
        file_control_->createDirectory(target, path);
}

void FileControlProxy::rename(
    common::FileTaskTarget target, const std::string& old_path, const std::string& new_path)
{
    if (!io_task_runner_->belongsToCurrentThread())
    {
        io_task_runner_->postTask(
            std::bind(&FileControlProxy::rename, shared_from_this(), target, old_path, new_path));
        return;
    }

    if (file_control_)
        file_control_->rename(target, old_path, new_path);
}

void FileControlProxy::remove(common::FileTaskTarget target,
                              std::shared_ptr<FileRemoveWindowProxy> remove_window_proxy,
                              const FileRemover::TaskList& items)
{
    if (!io_task_runner_->belongsToCurrentThread())
    {
        io_task_runner_->postTask(std::bind(
            &FileControlProxy::remove, shared_from_this(), target, remove_window_proxy, items));
        return;
    }

    if (file_control_)
        file_control_->remove(target, remove_window_proxy, items);
}

void FileControlProxy::transfer(std::shared_ptr<FileTransferWindowProxy> transfer_window_proxy,
                                FileTransfer::Type transfer_type,
                                const std::string& source_path,
                                const std::string& target_path,
                                const std::vector<FileTransfer::Item>& items)
{
    if (!io_task_runner_->belongsToCurrentThread())
    {
        io_task_runner_->postTask(
            std::bind(&FileControlProxy::transfer, shared_from_this(), transfer_window_proxy,
                      transfer_type, source_path, target_path, items));
        return;
    }

    if (file_control_)
        file_control_->transfer(transfer_window_proxy, transfer_type, source_path, target_path, items);
}

} // namespace client