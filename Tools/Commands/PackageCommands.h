#pragma once
#include "Commands/ToolCommandRunner.h"
namespace Catalpa::Tools {
    ToolCommandResult RunPackageCommand(std::span<const std::string>, CancellationToken);
}
