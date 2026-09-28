#include "DeadwaterGameMode.h"
#include "DeadwaterBoat.h"

ADeadwaterGameMode::ADeadwaterGameMode()
{
	// Until the on-foot character exists (milestone 2), players spawn straight into a boat.
	DefaultPawnClass = ADeadwaterBoat::StaticClass();
}
