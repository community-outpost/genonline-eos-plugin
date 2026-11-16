// SPDX-License-Identifier: MIT
// Unit tests of the linked account logic, driven by a fake EOS Connect backend.
#include "test_harness.h"

#include "core/host_buffer.h"
#include "core/linked_account_machine.h"

#include <cstring>
#include <string>
#include <vector>

using namespace genonline_eos;

namespace
{

struct FakeBackend : ConnectBackend
{
	bool acceptLogin = true;
	bool acceptCreateUser = true;
	std::vector<uint32_t> loginOperations;
	std::vector<std::string> loginTokens;
	std::vector<uint32_t> createUserOperations;

	bool StartLogin(uint32_t operation, const char* token) override
	{
		loginOperations.push_back(operation);
		loginTokens.push_back(token);
		return acceptLogin;
	}

	bool StartCreateUser(uint32_t operation) override
	{
		createUserOperations.push_back(operation);
		return acceptCreateUser;
	}
};

struct StatusRecorder
{
	std::vector<int32_t> results;
};

void GENONLINE_PLUGIN_CALL RecordStatus(void* userData, int32_t success)
{
	static_cast<StatusRecorder*>(userData)->results.push_back(success);
}

struct LogRecorder
{
	std::vector<std::string> lines;

	LogSink Sink()
	{
		return [this](int32_t, const std::string& text)
		{
			lines.push_back(text);
		};
	}

	bool Contains(const char* text) const
	{
		for (const std::string& line : lines)
		{
			if (line.find(text) != std::string::npos)
			{
				return true;
			}
		}
		return false;
	}
};

const char* const kToken = "SECRET_TOKEN_VALUE";
const char* const kUser = "0002aabbccddeeff0011223344556677";

} // namespace

TEST_CASE(buffer_copy_fits_with_its_nul)
{
	char buffer[6];
	std::memset(buffer, 'x', sizeof(buffer));
	CHECK(CopyToHostBuffer("abcde", buffer, 6));
	CHECK(std::strcmp(buffer, "abcde") == 0);
}

TEST_CASE(buffer_copy_refuses_a_value_without_room_for_its_nul)
{
	char buffer[5];
	std::memset(buffer, 'x', sizeof(buffer));
	CHECK(!CopyToHostBuffer("abcde", buffer, 5));
	CHECK(buffer[0] == 'x' && buffer[4] == 'x');
}

TEST_CASE(buffer_copy_refuses_missing_values_and_buffers)
{
	char buffer[8] = {};
	CHECK(!CopyToHostBuffer(nullptr, buffer, sizeof(buffer)));
	CHECK(!CopyToHostBuffer("", buffer, sizeof(buffer)));
	CHECK(!CopyToHostBuffer("abc", nullptr, sizeof(buffer)));
	CHECK(!CopyToHostBuffer("abc", buffer, 0));
}

TEST_CASE(status_once_reports_exactly_once)
{
	StatusRecorder recorder;
	StatusOnce status(&RecordStatus, &recorder);
	CHECK(status.IsArmed());
	status.Report(true);
	status.Report(false);
	CHECK(!status.IsArmed());
	CHECK(recorder.results.size() == 1);
	CHECK(recorder.results.size() == 1 && recorder.results[0] == GENONLINE_PLUGIN_TRUE);
	StatusOnce empty(nullptr, nullptr);
	empty.Report(false);
	CHECK(!empty.IsArmed());
}

TEST_CASE(sign_in_success_reports_true_once)
{
	FakeBackend backend;
	LogRecorder log;
	LinkedAccountMachine machine(backend, log.Sink());
	StatusRecorder status;
	machine.SignIn(kToken, &RecordStatus, &status);
	CHECK(backend.loginOperations.size() == 1);
	CHECK(backend.loginTokens.size() == 1 && backend.loginTokens[0] == kToken);
	CHECK(status.results.empty());
	CHECK(machine.IsBusy());
	CHECK(!machine.IsSignedIn());
	machine.OnLoginResult(backend.loginOperations[0], LoginOutcome::Success, kUser);
	CHECK(status.results.size() == 1 && status.results[0] == GENONLINE_PLUGIN_TRUE);
	CHECK(machine.IsSignedIn());
	CHECK(machine.ProductUserId() == kUser);
	CHECK(!machine.IsBusy());
}

TEST_CASE(sign_in_creates_the_user_when_none_exists)
{
	FakeBackend backend;
	LogRecorder log;
	LinkedAccountMachine machine(backend, log.Sink());
	StatusRecorder status;
	machine.SignIn(kToken, &RecordStatus, &status);
	machine.OnLoginResult(backend.loginOperations[0], LoginOutcome::UserNotFound, "");
	CHECK(backend.createUserOperations.size() == 1);
	CHECK(status.results.empty());
	machine.OnCreateUserResult(backend.createUserOperations[0], true, kUser);
	CHECK(status.results.size() == 1 && status.results[0] == GENONLINE_PLUGIN_TRUE);
	CHECK(machine.IsSignedIn());
	CHECK(machine.ProductUserId() == kUser);
}

TEST_CASE(sign_in_fails_when_creating_the_user_fails)
{
	FakeBackend backend;
	LogRecorder log;
	LinkedAccountMachine machine(backend, log.Sink());
	StatusRecorder status;
	machine.SignIn(kToken, &RecordStatus, &status);
	machine.OnLoginResult(backend.loginOperations[0], LoginOutcome::UserNotFound, "");
	machine.OnCreateUserResult(backend.createUserOperations[0], false, "");
	CHECK(status.results.size() == 1 && status.results[0] == GENONLINE_PLUGIN_FALSE);
	CHECK(!machine.IsSignedIn());
}

TEST_CASE(sign_in_fails_when_creating_the_user_cannot_start)
{
	FakeBackend backend;
	backend.acceptCreateUser = false;
	LogRecorder log;
	LinkedAccountMachine machine(backend, log.Sink());
	StatusRecorder status;
	machine.SignIn(kToken, &RecordStatus, &status);
	machine.OnLoginResult(backend.loginOperations[0], LoginOutcome::UserNotFound, "");
	CHECK(status.results.size() == 1 && status.results[0] == GENONLINE_PLUGIN_FALSE);
	CHECK(!machine.IsBusy());
}

TEST_CASE(sign_in_failure_reports_false_once)
{
	FakeBackend backend;
	LogRecorder log;
	LinkedAccountMachine machine(backend, log.Sink());
	StatusRecorder status;
	machine.SignIn(kToken, &RecordStatus, &status);
	machine.OnLoginResult(backend.loginOperations[0], LoginOutcome::Failure, "");
	CHECK(status.results.size() == 1 && status.results[0] == GENONLINE_PLUGIN_FALSE);
	CHECK(!machine.IsSignedIn());
	CHECK(machine.ProductUserId().empty());
}

TEST_CASE(sign_in_that_cannot_start_reports_false_at_once)
{
	FakeBackend backend;
	backend.acceptLogin = false;
	LogRecorder log;
	LinkedAccountMachine machine(backend, log.Sink());
	StatusRecorder status;
	machine.SignIn(kToken, &RecordStatus, &status);
	CHECK(status.results.size() == 1 && status.results[0] == GENONLINE_PLUGIN_FALSE);
	CHECK(!machine.IsBusy());
}

TEST_CASE(sign_in_without_a_token_reports_false_without_a_login)
{
	FakeBackend backend;
	LogRecorder log;
	LinkedAccountMachine machine(backend, log.Sink());
	StatusRecorder status;
	machine.SignIn(nullptr, &RecordStatus, &status);
	machine.SignIn("", &RecordStatus, &status);
	machine.Refresh(nullptr, &RecordStatus, &status);
	CHECK(backend.loginOperations.empty());
	CHECK(status.results.size() == 3);
	for (int32_t result : status.results)
	{
		CHECK(result == GENONLINE_PLUGIN_FALSE);
	}
}

TEST_CASE(sign_in_while_busy_reports_false_and_keeps_the_running_one)
{
	FakeBackend backend;
	LogRecorder log;
	LinkedAccountMachine machine(backend, log.Sink());
	StatusRecorder first;
	StatusRecorder second;
	machine.SignIn(kToken, &RecordStatus, &first);
	machine.SignIn(kToken, &RecordStatus, &second);
	CHECK(backend.loginOperations.size() == 1);
	CHECK(second.results.size() == 1 && second.results[0] == GENONLINE_PLUGIN_FALSE);
	machine.OnLoginResult(backend.loginOperations[0], LoginOutcome::Success, kUser);
	CHECK(first.results.size() == 1 && first.results[0] == GENONLINE_PLUGIN_TRUE);
}

TEST_CASE(abort_reports_false_for_a_pending_sign_in_and_ignores_late_results)
{
	FakeBackend backend;
	LogRecorder log;
	LinkedAccountMachine machine(backend, log.Sink());
	StatusRecorder status;
	machine.SignIn(kToken, &RecordStatus, &status);
	machine.Abort();
	CHECK(status.results.size() == 1 && status.results[0] == GENONLINE_PLUGIN_FALSE);
	machine.OnLoginResult(backend.loginOperations[0], LoginOutcome::Success, kUser);
	CHECK(status.results.size() == 1);
	CHECK(!machine.IsSignedIn());
	machine.Abort();
	CHECK(status.results.size() == 1);
}

TEST_CASE(abort_signs_out)
{
	FakeBackend backend;
	LogRecorder log;
	LinkedAccountMachine machine(backend, log.Sink());
	StatusRecorder status;
	machine.SignIn(kToken, &RecordStatus, &status);
	machine.OnLoginResult(backend.loginOperations[0], LoginOutcome::Success, kUser);
	machine.Abort();
	CHECK(!machine.IsSignedIn());
	CHECK(machine.ProductUserId().empty());
}

TEST_CASE(results_of_another_operation_are_ignored)
{
	FakeBackend backend;
	LogRecorder log;
	LinkedAccountMachine machine(backend, log.Sink());
	StatusRecorder status;
	machine.SignIn(kToken, &RecordStatus, &status);
	machine.OnLoginResult(backend.loginOperations[0] + 1, LoginOutcome::Success, kUser);
	machine.OnCreateUserResult(backend.loginOperations[0], true, kUser);
	CHECK(status.results.empty());
	CHECK(machine.IsBusy());
}

TEST_CASE(refresh_logs_in_again_for_the_same_user)
{
	FakeBackend backend;
	LogRecorder log;
	LinkedAccountMachine machine(backend, log.Sink());
	StatusRecorder status;
	machine.SignIn(kToken, &RecordStatus, &status);
	machine.OnLoginResult(backend.loginOperations[0], LoginOutcome::Success, kUser);
	StatusRecorder refresh;
	machine.Refresh("NEW_TOKEN", &RecordStatus, &refresh);
	CHECK(backend.loginOperations.size() == 2);
	CHECK(backend.loginTokens.size() == 2 && backend.loginTokens[1] == "NEW_TOKEN");
	CHECK(machine.IsSignedIn());
	machine.OnLoginResult(backend.loginOperations[1], LoginOutcome::Success, kUser);
	CHECK(refresh.results.size() == 1 && refresh.results[0] == GENONLINE_PLUGIN_TRUE);
	CHECK(machine.IsSignedIn());
	CHECK(machine.ProductUserId() == kUser);
}

TEST_CASE(refresh_for_another_user_fails_and_keeps_the_sign_in)
{
	FakeBackend backend;
	LogRecorder log;
	LinkedAccountMachine machine(backend, log.Sink());
	StatusRecorder status;
	machine.SignIn(kToken, &RecordStatus, &status);
	machine.OnLoginResult(backend.loginOperations[0], LoginOutcome::Success, kUser);
	StatusRecorder refresh;
	machine.Refresh("NEW_TOKEN", &RecordStatus, &refresh);
	machine.OnLoginResult(backend.loginOperations[1], LoginOutcome::Success, "0002ffffffffffffffffffffffffffff");
	CHECK(refresh.results.size() == 1 && refresh.results[0] == GENONLINE_PLUGIN_FALSE);
	CHECK(machine.IsSignedIn());
	CHECK(machine.ProductUserId() == kUser);
}

TEST_CASE(refresh_failure_keeps_the_sign_in)
{
	FakeBackend backend;
	LogRecorder log;
	LinkedAccountMachine machine(backend, log.Sink());
	StatusRecorder status;
	machine.SignIn(kToken, &RecordStatus, &status);
	machine.OnLoginResult(backend.loginOperations[0], LoginOutcome::Success, kUser);
	StatusRecorder refresh;
	machine.Refresh("NEW_TOKEN", &RecordStatus, &refresh);
	machine.OnLoginResult(backend.loginOperations[1], LoginOutcome::Failure, "");
	CHECK(refresh.results.size() == 1 && refresh.results[0] == GENONLINE_PLUGIN_FALSE);
	CHECK(machine.IsSignedIn());
}

TEST_CASE(refresh_never_creates_a_user_for_a_signed_in_user)
{
	FakeBackend backend;
	LogRecorder log;
	LinkedAccountMachine machine(backend, log.Sink());
	StatusRecorder status;
	machine.SignIn(kToken, &RecordStatus, &status);
	machine.OnLoginResult(backend.loginOperations[0], LoginOutcome::Success, kUser);
	StatusRecorder refresh;
	machine.Refresh("NEW_TOKEN", &RecordStatus, &refresh);
	machine.OnLoginResult(backend.loginOperations[1], LoginOutcome::UserNotFound, "");
	CHECK(backend.createUserOperations.empty());
	CHECK(refresh.results.size() == 1 && refresh.results[0] == GENONLINE_PLUGIN_FALSE);
}

TEST_CASE(refresh_without_a_user_signs_in)
{
	FakeBackend backend;
	LogRecorder log;
	LinkedAccountMachine machine(backend, log.Sink());
	StatusRecorder refresh;
	machine.Refresh(kToken, &RecordStatus, &refresh);
	machine.OnLoginResult(backend.loginOperations[0], LoginOutcome::UserNotFound, "");
	machine.OnCreateUserResult(backend.createUserOperations[0], true, kUser);
	CHECK(refresh.results.size() == 1 && refresh.results[0] == GENONLINE_PLUGIN_TRUE);
	CHECK(machine.IsSignedIn());
}

TEST_CASE(login_status_change_signs_out_the_user)
{
	FakeBackend backend;
	LogRecorder log;
	LinkedAccountMachine machine(backend, log.Sink());
	StatusRecorder status;
	machine.SignIn(kToken, &RecordStatus, &status);
	machine.OnLoginResult(backend.loginOperations[0], LoginOutcome::Success, kUser);
	machine.OnLoginStatusChanged("0002ffffffffffffffffffffffffffff", false);
	CHECK(machine.IsSignedIn());
	machine.OnLoginStatusChanged(kUser, false);
	CHECK(!machine.IsSignedIn());
	CHECK(machine.ProductUserId().empty());
}

TEST_CASE(status_callback_may_start_the_next_sign_in)
{
	FakeBackend backend;
	LogRecorder log;
	LinkedAccountMachine machine(backend, log.Sink());
	struct Context
	{
		LinkedAccountMachine* machine;
		StatusRecorder retry;
	};
	Context context{&machine, {}};
	machine.SignIn(kToken, [](void* userData, int32_t)
		{
			Context* inner = static_cast<Context*>(userData);
			inner->machine->SignIn(kToken, &RecordStatus, &inner->retry);
		}, &context);
	machine.OnLoginResult(backend.loginOperations[0], LoginOutcome::Failure, "");
	CHECK(backend.loginOperations.size() == 2);
	CHECK(context.retry.results.empty());
	CHECK(machine.IsBusy());
}

TEST_CASE(no_log_line_holds_the_token)
{
	FakeBackend backend;
	LogRecorder log;
	LinkedAccountMachine machine(backend, log.Sink());
	StatusRecorder status;
	machine.SignIn(kToken, &RecordStatus, &status);
	machine.OnLoginResult(backend.loginOperations[0], LoginOutcome::UserNotFound, "");
	machine.OnCreateUserResult(backend.createUserOperations[0], true, kUser);
	machine.Refresh(kToken, &RecordStatus, &status);
	machine.OnLoginResult(backend.loginOperations[1], LoginOutcome::Failure, "");
	CHECK(!log.lines.empty());
	CHECK(!log.Contains(kToken));
	CHECK(log.Contains("18 bytes"));
}

int main(int argc, char** argv)
{
	return testing::RunMain(argc, argv);
}
