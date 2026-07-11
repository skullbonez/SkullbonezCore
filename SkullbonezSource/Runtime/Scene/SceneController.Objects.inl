/*
Purpose:
  Declares SceneController's coordinated entity, physics, and render-store
  operations separately from navigation and request policy.

Mental model:
  These are operations on concrete SceneController-owned stores, not a
  nested context or replacement object-model façade.

Invariants:
  - SceneEntityStore, physics body/collider rows, and render rows retain the
    same dense count after every successful creation or deletion.
  - PhysicsSceneObjectId remains durable identity; modelIndex parameters are
    short-lived row hints at cold or presentation boundaries.
  - Hot physics loops consume physics-owned arrays, never this declaration
    surface or callbacks into SceneController.
*/
private:
Rendering::RenderInstanceStore m_renderInstanceStore; // Render snapshot in scene/model order, owned outside physics.
int m_activeGameModelCapacity = DEFAULT_GAME_MODEL_CAPACITY; // Configured model cap used by append/reserve guards.
void ReserveForActiveGameModelCapacity();
const Basics::SceneBehaviorGroup& BehaviorGroupAt( int modelIndex ) const;
int ResolveBehaviorGroupRootModelIndex( const Basics::SceneBehaviorGroup& group ) const;
// Owner boundary: SceneEntityStore owns fixed-tree grouping. Body-store
// import receives only derived row hints, never scene metadata accessors.
std::vector<Physics::ModelRowHint> BuildFixedTreeReleaseRootsForReload() const;
std::vector<const char*> BuildDiagnosticNamesForReload() const;
bool RefreshPhysicsBodyStoreFromAuthoredDescriptors();
// Private body-only repair is reserved for scene-owned projection
// phases. Public tool/runtime reads must use an explicit owner boundary
// before borrowing PhysicsEngine store views.
bool RepairPhysicsBodyTopology();
int FixedTreeReleaseRootForModelIndex( int modelIndex ) const;
void RefreshRenderInstances();
Basics::SceneEntityStore& SceneEntities();
const Basics::SceneEntityStore& SceneEntities() const;
void AssertSceneCreationTopology( int expectedCount ) const;

public:
void ApplyRuntimeConfig( const Basics::EngineConfig& config );
// One preflighted scene-creation command publishes metadata, physics, and
// render rows together. Lane R input failures leave every owner unchanged;
// a mismatched owner count is a fatal topology invariant.
SceneEntityCreateResult TryCreateSceneEntity( Basics::SceneEntityCreateDesc entity,
                                              Physics::PhysicsBodyCreateDesc bodyDesc,
                                              Physics::PhysicsColliderCreateDesc colliderDesc );
// Cold scene/editor deletion removes the entity's physics, metadata,
// presentation, and render rows as one swap-last transaction.
bool DestroySceneEntity( Physics::PhysicsBodyHandle body );
void Clear();
void PrepareRenderInstances();
// Legacy object-follow cameras can outlive the model slots they track.
// Returns false only for an absent slot; a present model without a body is
// store-topology drift and still fails through the fatal invariant lane.
bool TryGetModelPosition( int index, Math::Vector::Vector3& outPosition ) const;
// Scene entity count is the stable model-slot count shared by scene files,
// editor picks, replay streams, and cold owner-repair boundaries.
int SceneEntityCount() const;
// These compatibility queries read SceneEntityStore-owned stable behavior
// groups; callers receive a row only when their operation requires one.
Basics::SceneBehaviorGroupKind GroupKindAt( int modelIndex ) const;
Physics::PhysicsSceneObjectId GroupRootObjectIdAt( int modelIndex ) const;
int GroupPartIndexAt( int modelIndex ) const;
bool IsSimpleRagdollPart( int modelIndex ) const;
bool IsSimpleRagdollTorso( int modelIndex ) const;
int RagdollRootModelIndexForPart( int modelIndex ) const;
bool TryFindSimpleRagdollPart( int selectedModelIndex, int partIndex, int& outModelIndex ) const;
int GatherGroupMemberIndices( int selectedModelIndex, int* outIndices, int maxIndices ) const;
#ifdef _DEBUG
bool TryGetPhysicsDiagnosticsModelName( int index, const char*& outName ) const;
void FillPhysicsDiagnosticsNames( int bodyCount, std::vector<const char*>& outNames ) const;
#endif
Basics::MainMemoryGameObjectStats CollectMemoryStats() const;
// SceneController uses this narrow presentation-owner command while it
// coordinates replay topology with physics and entity owners.
bool CanTrimPresentationRowsForSceneRestore( int modelCount ) const;
bool TrimPresentationRowsForSceneRestore( int modelCount );
void CaptureReplaySolverWorldSnapshot( Basics::ReplaySolverWorldSnapshot& outSnapshot ) const;
bool RestoreReplaySolverWorldSnapshot( const Basics::ReplaySolverWorldSnapshot& snapshot );
// Explicit cold owner boundary before tool or picker code asks for body
// handles and collider bounds. Read-only store accessors do not repair.
bool RepairPhysicsBodyAndColliderTopology();
// Current prepared collider snapshot. Hot render passes use this after
// PrepareRenderInstances() instead of invoking topology repair mid-submit.
const Physics::PhysicsBodyStore& BodyStore() const;
const Physics::ColliderStore& Colliders() const;
// Current prepared render snapshot. Call PrepareRenderInstances() before frame
// passes; cold callers that need an ensured snapshot use GetRenderInstanceStore().
Rendering::RenderInstanceStore& MutableRenderInstances();
const Rendering::RenderInstanceStore& RenderInstances() const;
// Replay presentation samples are one-frame render overrides. The collection
// validates replay body identity before mutating its render snapshot so scrub
// and prediction code cannot redirect stale model slots.
bool TryQueueReplayRenderPoseOverride( int modelIndex,
                                       uint32_t replayBodyId,
                                       const Math::Vector::Vector3& position,
                                       const Math::Orientation::Quaternion& orientation );
const std::vector<Rendering::RenderInstancePresentationRecord>& RenderPresentationRecords() const
{
    return m_renderInstanceStore.PresentationRecords();
}
const Rendering::RenderInstanceStore& GetRenderInstanceStore();
double GetSceneKineticEnergy();
void NotifyFixedContact( int modelIndex, float highlightSeconds );
void TickContactHighlights( int modelCount, float deltaSeconds );
void NotifyAudioContact( int modelIndex, float highlightSeconds );
// Runtime-tool edge: ray tools release authored fixed tree props through
// PhysicsBodyStore; presentation reads the store/render snapshot instead of
// forcing a per-release model-side body projection.
bool ReleaseAttachedFixedTreeParts( int sourceIndex,
                                    float releaseImpulseStrength,
                                    const Math::Vector::Vector3& seedLinearVelocity,
                                    const Math::Vector::Vector3& seedAngularVelocity );

void BeginCollisionVisualFrame();
void EndCollisionVisualFrame();
