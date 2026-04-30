#include "blocktype/Backend/BackendBase.h"

namespace blocktype::backend {

BackendBase::BackendBase(const BackendOptions& Opts, DiagnosticsEngine& Diags)
  : Opts(Opts), Diags(Diags) {}

} // namespace blocktype::backend
