/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file Animation.h Header for Animation, AnimationManager, AnimationPlayer
 * and related structures.
 */

#include "KeyInterpolation.h"
#include "OrderedStringMap.h"
#include "ParameterBlock.h"
#include "Resource.h"
#include "SkeletonComponent.h"
#include "Texture.h"
#include "Types.h"

#include <map>

namespace ToolKit
{
  /**
   * A transformation key that is part of an Animation resource.
   *
   * A key carries all three channels, and it also states how the segments around it are
   * interpolated (see KeyInterp). New members must be appended at the end: the key array is
   * serialized as a raw struct dump, and the reader relies on the existing prefix keeping its
   * layout so files written by an older build keep loading (see Animation::DeSerializeImp).
   */
  struct Key
  {
    int m_frame = 0;       //!< Order / Frame of the key.
    Vec3 m_position;       //!< Position of the transform.
    Quaternion m_rotation; //!< Rotation of the transform.
    Vec3 m_scale;          //!< Scale of the transform.

    /** Interpolation of this key with its neighbours. Linear is the default. */
    KeyInterp m_interp = KeyInterp::Linear;
  };

  typedef std::vector<Key> KeyArray;
  // Ordered by insertion; each bone key track keeps the order it was loaded or
  // added with. See OrderedStringMap.
  typedef OrderedStringMap<KeyArray> BoneKeyArrayMap;

  /**
   * A key that drives one parameter value instead of a node transform.
   *
   * A parameter is a ParameterVariant (float, int, uint, bool, vec2, vec3, vec4), and its value is
   * carried here in four floats whatever the type is, so a track stores and serializes uniformly.
   * m_type says how to read them back and whether the value steps or interpolates.
   */
  struct ParamKey
  {
    int m_frame                       = 0;
    ParameterVariant::VariantType m_type = ParameterVariant::VariantType::Float;
    Vec4 m_value;
  };

  typedef std::vector<ParamKey> ParamKeyArray;
  typedef OrderedStringMap<ParamKeyArray> ParamKeyArrayMap;

  /**
   * The class that represents animations which can be played with
   * AnimationPlayer. Alter's Entity Node transforms or Skeleton / Bone
   * transforms to apply the animation to the corresponding Entity.
   */
  class TK_API Animation : public Resource
  {
   public:
    TKDeclareClass(Animation, Resource);

    /**
     * Empty constructor.
     */
    Animation();

    /**
     * Constructs an Animation object from the file.
     * @param file Disk file to load the resource from.
     */
    explicit Animation(const String& file);

    /**
     * Uninitiate and frees the memory.
     */
    virtual ~Animation();

    /**
     * Sets the Node's transform from the animation based on time.
     * @param node Node to be transformed.
     * @param time Time to fetch the transformation from.
     * @param keyName Track to sample. A clip stores one track per animated node, named after it,
     * so a multi node clip drives each node from its own curve. Empty or unknown names fall back
     * to the first track, which keeps single curve clips working.
     */
    void GetPose(Node* node, float time, const String& keyName = "");

    /**
     * Sets the Skeleton's transform from the animation based on time.
     * @param skeleton SkeletonPtr to be transformed.
     */
    void GetPose(const SkeletonComponentPtr& skeleton, float time);

    /**
     * Samples one track at the given time, honouring the interpolation mode of its keys.
     * This is the single sampling path of a node track: the runtime pose, root motion and the
     * editor's dope sheet preview all go through it, so preview and playback cannot drift.
     * @param keys Track to sample.
     * @param time Time to fetch the transformation from.
     * @param pos Output position.
     * @param rot Output rotation.
     * @param scale Output scale.
     * @return False when the track is empty, the inputs are untouched in that case.
     */
    bool SampleTrack(const KeyArray& keys, float time, Vec3& pos, Quaternion& rot, Vec3& scale) const;

    /**
     * Sets the Node's transform from the animation based on frame.
     * @see GetPose(Node* node, float time)
     */
    void GetPose(Node* node, int frame);

    void Load() override; //!< Loads the animation data from file.

    /**
     * Set the resource to initiated state.
     * @param flushClientSideArray unused.
     */
    void Init(bool flushClientSideArray = false) override;

    /**
     * Set the resource to uninitiated state and removes the keys.
     */
    void UnInit() override;

    /**
     * Finds nearest keys and interpolation ratio for current time.
     * @param keys animation key array.
     * @param key1 output key 1.
     * @param key2 output key 2.
     * @param ratio output ratio.
     * @param t time to search keys for.
     */
    void GetNearestKeys(const KeyArray& keys, int& key1, int& key2, float& ratio, float t);

    /**
     * Writes or removes the key at the given frame on a track, keeping the track sorted by frame.
     * The track is created when it does not exist yet and a key is given.
     * @param keyName Name of the track to edit.
     * @param frame Frame of the key.
     * @param key Key to write, or nullptr to remove the key at that frame.
     * @return True when the edit was applied.
     */
    bool SetKey(const String& keyName, int frame, const Key* key);

    // Parameter tracks.
    //////////////////////////////////////////

    /**
     * Writes or removes a parameter key, the counterpart of SetKey() for parameter tracks.
     * @param trackName Track id, see ResolveParamTrack() for the accepted forms.
     * @param frame Frame of the key.
     * @param key Key to write, or nullptr to remove the key at that frame.
     * @return True when the edit was applied.
     */
    bool SetParamKey(const String& trackName, int frame, const ParamKey* key);

    /**
     * Samples a parameter track at the given time. Float and vector types interpolate, everything
     * else holds the previous key.
     * @param trackName Track id.
     * @param time Time to sample at, in seconds.
     * @param value Output, the four floats carrying the value.
     * @param type Output, the variant type the value belongs to.
     * @return False when the track does not exist or holds no keys.
     */
    bool GetParamValue(const String& trackName,
                       float time,
                       Vec4& value,
                       ParameterVariant::VariantType& type) const;

    /**
     * Resolves the parameter a track id addresses on an entity. Accepted forms:
     * "<entity>.<param>", "<entity>.<componentClass>.<param>" and
     * "<entity>.MaterialComponent.<materialIndex>.<param>".
     * @param entity Entity the track belongs to.
     * @param trackName Track id.
     * @return The variant to read or write, or nullptr when the id addresses nothing on the entity.
     */
    ParameterVariant* ResolveParamTrack(EntityPtr entity, const String& trackName);

    /**
     * Applies every parameter track of the entity at the given time, writing each value through its
     * variant so the parameter's change callbacks run.
     */
    void ApplyParamTracks(EntityPtr entity, float time);

    /** States if a variant type can be keyed at all. */
    static bool IsParamTypeAnimatable(ParameterVariant::VariantType type);

    /** Packs a variant value into the four floats a ParamKey carries. */
    static bool PackParamValue(const ParameterVariant& var, Vec4& value);

    /**
     * Writes a packed value back through the variant, which fires the change callbacks of the
     * parameter (material caches, light buffers) exactly like an edit in the inspector would.
     * @return False when the type does not match the variant.
     */
    static bool UnpackParamValue(ParameterVariant& var,
                                 ParameterVariant::VariantType type,
                                 const Vec4& value);

   protected:
    void CopyTo(Resource* other) override;

    XmlNode* SerializeImp(XmlDocument* doc, XmlNode* parent) const override;
    XmlNode* DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent) override;

   public:
    /**
     * A map that holds bone names and their corresponding keys
     * for this animation.
     */
    BoneKeyArrayMap m_keys;

    /**
     * Parameter tracks, keyed by track id ("<entity>.<param>", "<entity>.<component>.<param>" or
     * "<entity>.MaterialComponent.<index>.<param>"). Kept apart from m_keys so a node curve and a
     * parameter curve never share a name space.
     */
    ParamKeyArrayMap m_paramKeys;

    float m_fps      = 30.0f; //!< Frames to display per second.
    float m_duration = 0.0f;  //!< Duration of the animation.

    /**
     * Name of the key that carries root motion. When set, its displacement
     * is applied to the entity itself (see AnimationPlayer::ApplyRootMotion).
     * Empty means this animation carries no root motion.
     */
    String m_rootKey;
  };

  /**
   * The class responsible for managing
   * the life time and storing initial instances of the Animation resources.
   */
  class TK_API AnimationManager : public ResourceManager
  {
   public:
    AnimationManager();
    virtual ~AnimationManager();
    bool CanStore(ClassMeta* Class) override;
  };

  /**
   * The class that represents the current state of the animation such as its
   * current time
   */
  class TK_API AnimRecord
  {
    friend class AnimationPlayer;
    friend class AnimControllerComponent;
    friend class RenderJobProcessor;

   public:
    AnimRecord();  //!< Default constructor, only assigns a unique id.
    ~AnimRecord(); //!< Default destructor, releases the id.

    /**
     * Construct an animation record for the entity with given animation.
     * @param entity Is the entity to play the animation on.
     * @param anim Is the animation to play for the record.
     */
    void Construct(EntityPtr entity, AnimationPtr anim);

   protected:
    /**
     * Data block holding necessary information for blending.
     */
    struct BlendingData
    {
      AnimRecordPtr recordToBeBlended = nullptr; //!< AnimRecord that is being blended by another record
      AnimRecordPtr recordToBlend     = nullptr; //!< AnimRecord that is blending the current record
      float blendTotalDurationInSec   = -1.0f;   //!< Total duration of blending
      float blendCurrentDurationInSec = -1.0f;   //!< Current time of blending (Decreasing from total duration to zero)
    };

   public:
    /**
     * Current time of the animation expressed in seconds.
     */
    float m_currentTime    = 0.0f;
    bool m_loop            = false; //!< States if the animation mean to be looped.
    float m_timeMultiplier = 1.0f;  //!< Speed multiplier for animation.
    bool m_applyRootMotion = false; //!< Apply the skeleton's root bone displacement to the entity.
    AnimationPtr m_animation;       //!< Animation to play.
    EntityWeakPtr m_entity;

    /**
     * Enums that represent's the current state of the Animation in the
     * AnimationPlayer.
     */
    enum class State
    {
      Play,   //!< Animation is playing.
      Pause,  //!< Animation is paused.
      Rewind, //!< Animation will be rewind by the AnimationPlayer.
      Stop    //!< Stopped playing and will be removed from the AnimationPlayer.
    };

    State m_state = State::Play; //!< Current state of the animation.
    ObjectId m_id;               //!< Unique id for the animation.

   protected:
    BlendingData m_blendingData;
    float m_prevRootMotionTime = 0.0f; //!< Last applied root motion sample time (seconds).
  };

  /**
   * The class that is responsible playing animation records
   * and updating transformations of the corresponding Entities.
   */
  class TK_API AnimationPlayer
  {
   public:
    AnimationPlayer();  //!< Default constructor empty.
    ~AnimationPlayer(); //!< Default destructor that destroy all stored data.

    void Destroy();                  //!< Clears all record data stored.
    AnimRecordPtrArray GetRecords(); //!< Access to copy of animation records.

    /**
     * Adds a record to the player.
     * @param rec AnimRecord data.
     */
    void AddRecord(AnimRecordPtr rec);

    /**
     * Removes the AnimRecord with the given id.
     * @param id Id of the AnimRecord.
     */
    void RemoveRecord(ObjectId id);

    /**
     * Removes the given AnimRecord.
     * @param rec Record to remove.
     */
    void RemoveRecord(const AnimRecord& rec);

    /**
     * Update all the records in the player and apply transforms
     * to corresponding entities.
     * @param deltaTimeSec The delta time in seconds for
     * increment for each record.
     * While the records are paused, none of them is advanced (see Pause).
     */
    void Update(float deltaTimeSec);

    /**
     * Pauses every record: while paused, Update advances none of them -- record
     * times, blend countdowns and root motion all stay as they are -- and the
     * animation data filled by the last update is kept, so everything that
     * reads it keeps its current state. Time is neither skipped nor replayed,
     * so Resume carries on from the exact time the records were paused at.
     */
    void Pause();

    /**
     * Resumes the paused records from the time they were paused at.
     * No effect when they are not paused.
     */
    void Resume();

    /**
     * States if the records are paused (see Pause).
     * @return True while no record advances.
     */
    bool IsPaused() const;

    /**
     * Checks if the record exist.
     * @param id Is the id of the AnimRecord to check.
     * @return The index of the record, if it cannot find, returns -1.
     */
    int Exist(ObjectId id) const;

    /**
     * Returns animation data texture for given skeleton and animation.
     * Data is hold as skeleton - animation pair.
     * @param skelID is the skeleton to look for.
     * @param animID is the animation to look for.
     * @return Found data texture for the pair or nullptr.
     */
    DataTexturePtr GetAnimationDataTexture(ObjectId skelID, ObjectId animID);

   private:
    /**
     * Clears all animation records.
     */
    void ClearAnimRecords();

    /**
     * Applies root motion (node key displacement) to the record's entity nodes.
     */
    void ApplyRootMotion(AnimRecordPtr record);

    /**
     * Add data texture of animation for skeleton
     */
    void AddAnimationData(EntityWeakPtr ntt, AnimationPtr anim);

    /**
     * Removes the unnecessary data textures
     */
    void UpdateAnimationData();

    /**
     * Clears the animation data textures
     */
    void ClearAnimationData();

    /**
     * Creates and returns animation data texture for given skeleton and animation
     */
    DataTexturePtr CreateAnimationDataTexture(SkeletonPtr skeleton, AnimationPtr anim);

   public:
    /** Global time multiplier for all track in the player. */
    float m_timeMultiplier = 1.0f;

   private:
    // Storage for the AnimRecord objects.
    AnimRecordPtrArray m_records;

    // Storage for animation data (skeleton id - animation id pair)
    std::map<std::pair<ObjectId, ObjectId>, DataTexturePtr> m_animTextures;

    // True while Pause holds every record: Update advances none in this state.
    bool m_paused = false;
  };

} // namespace ToolKit
