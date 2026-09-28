#include "RiptideGameMode.h"
#include "RiptideBoat.h"

ARiptideGameMode::ARiptideGameMode()
{
	// Until the on-foot character exists (milestone 2), players spawn straight into a boat.
	DefaultPawnClass = ARiptideBoat::StaticClass();
}
