Local additions to the Dolphin checkout in source/dolphin (upstream commit 86d554802d7560acf12b5c5a2b3acb344069b5ce).
Copy DolphinRVZ.cpp and DolphinRVZ.h into Source/Core/DiscIO/ and apply DiscIO-CMakeLists.patch (git apply) to build DolphinRVZ.dll.
These files are part of Dolphin's GPL-2.0-or-later code base; see LICENSES/ and THIRD_PARTY_NOTICES.md.
DolphinRVZ.dll exports DolphinDiscConvert (ISO/RVZ/WIA/GCZ output) and the older DolphinRVZConvert (ISO/RVZ).
