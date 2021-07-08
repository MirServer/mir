/*
 * Copyright © 2016-2021 Canonical Ltd.
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

#ifndef MIROIL_PROMPT_SESSION_MANAGER_H
#define MIROIL_PROMPT_SESSION_MANAGER_H

#include <miral/application.h>

#include <memory>

namespace mir { namespace scene { class PromptSessionManager; class PromptSession;} }

namespace miroil {
    
class PromptSessionManager
{
public:
    PromptSessionManager(std::shared_ptr<mir::scene::PromptSessionManager> const& prompt_session_manager);
    PromptSessionManager(PromptSessionManager const& src);
    PromptSessionManager(PromptSessionManager&& src);
    ~PromptSessionManager();

    auto operator=(PromptSessionManager const& src) -> PromptSessionManager&;
    auto operator=(PromptSessionManager&& src) -> PromptSessionManager&;

    bool operator==(PromptSessionManager const& other);

    auto application_for(std::shared_ptr<mir::scene::PromptSession> const& prompt_session) const -> miral::Application;
    void resume_prompt_session(std::shared_ptr<mir::scene::PromptSession> const& prompt_session) const;
    void stop_prompt_session(std::shared_ptr<mir::scene::PromptSession> const& prompt_session) const;
    void suspend_prompt_session(std::shared_ptr<mir::scene::PromptSession> const& prompt_session) const;    

private:
    std::shared_ptr<mir::scene::PromptSessionManager> prompt_session_manager;
};
}

#endif //MIROIL_PROMPT_SESSION_MANAGER_H
