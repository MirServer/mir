/*
 * Copyright © 2021 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "miroil/prompt_session_manager.h"
#include "mir/scene/prompt_session_manager.h"

miroil::PromptSessionManager::PromptSessionManager(std::shared_ptr<mir::scene::PromptSessionManager> const& prompt_session_manager) 
:    prompt_session_manager{prompt_session_manager}
{
}

miroil::PromptSessionManager::PromptSessionManager(PromptSessionManager&& /*src*/) = default;

miroil::PromptSessionManager::PromptSessionManager(PromptSessionManager const& /*src*/) = default;

miroil::PromptSessionManager::~PromptSessionManager() = default;

bool miroil::PromptSessionManager::operator==(PromptSessionManager const& other)
{
    return prompt_session_manager == other.prompt_session_manager;
}

miral::Application miroil::PromptSessionManager::application_for(std::shared_ptr<mir::scene::PromptSession> const& prompt_session) const
{
    return prompt_session_manager->application_for(prompt_session);
}

void miroil::PromptSessionManager::stop_prompt_session(std::shared_ptr<mir::scene::PromptSession> const& prompt_session) const
{
    prompt_session_manager->stop_prompt_session(prompt_session);
}

void miroil::PromptSessionManager::suspend_prompt_session(std::shared_ptr<mir::scene::PromptSession> const& prompt_session) const
{
    prompt_session_manager->suspend_prompt_session(prompt_session);
}

void miroil::PromptSessionManager::resume_prompt_session(std::shared_ptr<mir::scene::PromptSession> const& prompt_session) const
{
    prompt_session_manager->resume_prompt_session(prompt_session);
}
