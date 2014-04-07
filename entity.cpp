#include <stdlib.h>
#include <math.h>

#include "vmath.h"
#include "mesh.h"
#include "entity.h"
#include "render.h"
#include "world.h"
#include "engine.h"
#include "anim_state_control.h"
#include "character_controller.h"
#include "bounding_volume.h"

#include "bullet/btBulletCollisionCommon.h"
#include "bullet/btBulletDynamicsCommon.h"
#include "bullet/BulletCollision/CollisionDispatch/btCollisionObject.h"
#include "console.h"


extern uint16_t                sounds_played;

entity_p Entity_Create()
{
    entity_p ret = (entity_p)calloc(1, sizeof(entity_t));
    ret->frame_time = 0.0;
    ret->move_type = MOVE_ON_FLOOR;
    Mat4_E(ret->transform);
    ret->active = 1;
    
    ret->self = (engine_container_p)malloc(sizeof(engine_container_t));
    ret->self->next = NULL;
    ret->self->object = ret;
    ret->self->object_type = OBJECT_ENTITY;
    ret->self->room = NULL;
    ret->self->collide_flag = 0;
    ret->bv = BV_Create();
    ret->bv->transform = ret->transform;
    ret->bt_body = NULL;
    ret->character = NULL;
    ret->smooth_anim = 1;
    ret->current_sector = NULL;
    
    ret->lerp = 0.0;
    ret->next_bf = NULL;
    
    ret->bf.bone_tag_count = 0;;
    ret->bf.bone_tags = 0;
    vec3_set_zero(ret->bf.bb_max);     
    vec3_set_zero(ret->bf.bb_min);  
    vec3_set_zero(ret->bf.centre);  
    vec3_set_zero(ret->bf.pos);  
    vec4_set_zero(ret->collision_offset.m_floats);                              // not an error, really btVector3 has 4 points in array
    vec4_set_zero(ret->speed.m_floats);
    
    return ret;
}


void Entity_Clear(entity_p entity)
{
    if(entity)
    {
        if(entity->bv)
        {
            BV_Clear(entity->bv);
            free(entity->bv);
            entity->bv = NULL;
        }
        
        if(entity->model && entity->bt_body)
        {
            for(int i=0;i<entity->model->mesh_count;i++)
            {
                btRigidBody *body = entity->bt_body[i];
                if(body)
                {
                    body->setUserPointer(NULL);           
                    if(body && body->getMotionState())
                    {
                        delete body->getMotionState();
                    }
                    if(body && body->getCollisionShape())
                    {
                        delete body->getCollisionShape();
                    }

                    bt_engine_dynamicsWorld->removeRigidBody(body);
                    delete body;
                    entity->bt_body[i] = NULL;
                }
            }
        }
        
        
        if(entity->character)
        {
            Character_Clean(entity);
        }
        
        if(entity->self)
        {
            free(entity->self);
            entity->self = NULL;
        }
        
        if(entity->bf.bone_tag_count)
        {
            free(entity->bf.bone_tags);
            entity->bf.bone_tags = NULL;
            entity->bf.bone_tag_count = 0;
        }
    }
}


void Entity_Enable(entity_p ent)
{
    int i;
    
    if(ent->active)
    {
        return;
    }
    
    for(i=0;i<ent->bf.bone_tag_count;i++)
    {
        btRigidBody *b = ent->bt_body[i];
        if(b)
        {
            bt_engine_dynamicsWorld->addRigidBody(b);
        }
    }
    
    ent->hide = 0;
    ent->active = 1;
}

void Entity_Disable(entity_p ent)
{
    int i;
    
    if(!ent->active)
    {
        return;
    }
    
    for(i=0;i<ent->bf.bone_tag_count;i++)
    {
        btRigidBody *b = ent->bt_body[i];
        if(b)
        {
            bt_engine_dynamicsWorld->removeRigidBody(b);
        }
    }
    
    ent->hide = 1;
    ent->active = 0;
}


void Entity_UpdateRoomPos(entity_p ent)
{
    btScalar pos[3], *v;
    room_p new_room;
    room_sector_p new_sector;
    
    v = ent->collision_offset.m_floats;
    Mat4_vec3_mul_macro(pos, ent->transform, v);
    new_room = Room_FindPosCogerrence(&engine_world, pos, ent->self->room);
    if(new_room)
    {
        new_sector = Room_GetSectorXYZ(new_room, pos);
        if(new_room != new_sector->owner_room)
        {
            new_room = new_sector->owner_room;
        }
        ent->self->room = new_room;
        if(ent->current_sector != new_sector)
        {
            ent->current_sector = new_sector;
            if(new_sector && (ent->flags & ENTITY_IS_ACTIVE) && (ent->flags & ENTITY_CAN_TRIGGER))
            {
                Entity_ParseFloorData(ent, &engine_world);
            }
        }
    }
}


void Entity_UpdateRigidBody(entity_p ent)
{
    int i;
    btScalar tr[16];
    btTransform	bt_tr;
    room_p old_room;
    
    if(!ent->model || !ent->bt_body || ((ent->model->animations->frames_count == 1) && (ent->model->animation_count == 1)))
    {
        return;
    }

    old_room = ent->self->room;
    if(!ent->character)
    {
        vec3_add(ent->collision_offset.m_floats, ent->bf.bb_min, ent->bf.bb_max);
        ent->collision_offset.m_floats[0] /= 2.0;
        ent->collision_offset.m_floats[1] /= 2.0;
        ent->collision_offset.m_floats[2] /= 2.0;
    }
    Entity_UpdateRoomPos(ent);

#if 1
    if(!ent->character && (ent->self->room != old_room))
    {
        if((ent->self->room != NULL) && !Room_IsOverlapped(ent->self->room, old_room))
        {
            if(ent->self->room && old_room)
            {
                Room_RemoveEntity(old_room, ent);
            }
            if(ent->self->room)
            {
                Room_AddEntity(ent->self->room, ent);
            }
        }
    }
#endif
    
    for(i=0;i<ent->model->mesh_count;i++)
    {
        if(ent->bt_body[i])
        {
            Mat4_Mat4_mul_macro(tr, ent->transform, ent->bf.bone_tags[i].full_transform);
            bt_tr.setFromOpenGLMatrix(tr);
            ent->bt_body[i]->setCollisionFlags(ent->bt_body[i]->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
            ent->bt_body[i]->setWorldTransform(bt_tr);
        }
    }
}


void Entity_UpdateRotation(entity_p entity)
{
    btScalar R[4], Rt[4], temp[4];
    btScalar sin_t2, cos_t2, t;
    btScalar *up_dir = entity->transform + 8;                                   // OZ
    btScalar *view_dir = entity->transform + 4;                                 // OY
    btScalar *right_dir = entity->transform + 0;                                // OX
    int i;
    
    i = entity->angles[0] / 360.0;
    i = (entity->angles[0] < 0.0)?(i-1):(i);
    entity->angles[0] -= 360.0 * i;
    
    i = entity->angles[1] / 360.0;
    i = (entity->angles[1] < 0.0)?(i-1):(i);
    entity->angles[1] -= 360.0 * i;
    
    i = entity->angles[2] / 360.0;
    i = (entity->angles[2] < 0.0)?(i-1):(i);
    entity->angles[2] -= 360.0 * i;
    
    t = entity->angles[0] * M_PI / 180.0;
    sin_t2 = sin(t);
    cos_t2 = cos(t);
    
    /*
     * LEFT - RIGHT INIT
     */
    
    view_dir[0] =-sin_t2;                                                       // OY - view
    view_dir[1] = cos_t2;
    view_dir[2] = 0.0;
    view_dir[3] = 0.0;
        
    right_dir[0] = cos_t2;                                                      // OX - right
    right_dir[1] = sin_t2;
    right_dir[2] = 0.0;
    right_dir[3] = 0.0;
    
    up_dir[0] = 0.0;                                                            // OZ - up
    up_dir[1] = 0.0;
    up_dir[2] = 1.0;
    up_dir[3] = 0.0; 
    
    if(entity->angles[1] != 0.0)
    {
        t = entity->angles[1] * M_PI / 360.0;                                   // UP - DOWN
        sin_t2 = sin(t);
        cos_t2 = cos(t);
        R[3] = cos_t2;
        R[0] = right_dir[0] * sin_t2;
        R[1] = right_dir[1] * sin_t2;
        R[2] = right_dir[2] * sin_t2;
        vec4_sop(Rt, R); 

        vec4_mul(temp, R, up_dir); 
        vec4_mul(up_dir, temp, Rt); 
        vec4_mul(temp, R, view_dir); 
        vec4_mul(view_dir, temp, Rt); 
    }
    
    if(entity->angles[2] != 0.0)
    {
        t = entity->angles[2] * M_PI / 360.0;                                   // ROLL
        sin_t2 = sin(t);
        cos_t2 = cos(t);
        R[3] = cos_t2;
        R[0] = view_dir[0] * sin_t2;
        R[1] = view_dir[1] * sin_t2;
        R[2] = view_dir[2] * sin_t2;
        vec4_sop(Rt, R); 

        vec4_mul(temp, R, right_dir); 
        vec4_mul(right_dir, temp, Rt); 
        vec4_mul(temp, R, up_dir); 
        vec4_mul(up_dir, temp, Rt);   
    }
    
    view_dir[3] = 0.0;
    right_dir[3] = 0.0;
    up_dir[3] = 0.0; 
}


/**
 * @FIXME: find rotation command and implement it!!!
 * Check the entity transformation anim commands and writes one to the tr[4] array. 
 * @param entity - entity pointer
 * @param anim - animation number, where we search command
 * @param frame - frame number for correct condition check
 * @param tr - x, y, z offset + OZ rotation in deg.
 */
void Entity_GetAnimCommandTransform(entity_p entity, int anim, int frame, btScalar tr[4])
{
    vec4_set_zero(tr);
    
    if((engine_world.anim_commands_count == 0) || 
       (entity->model->animations[entity->current_animation].num_anim_commands > 255))
    {
        return;                                                                 // If no anim commands or current anim has more than 255 (according to TRosettaStone).
    }
        
    animation_frame_p af  = entity->model->animations + anim;
    uint32_t count        = af->num_anim_commands;
    int16_t *pointer      = engine_world.anim_commands + af->anim_command;
    
    for(uint32_t i = 0; i < count; i++, pointer++)
    {
        switch(*pointer)
        {
            case TR_ANIMCOMMAND_SETPOSITION:
                // This command executes ONLY at the end of animation. 
                if(frame == af->frames_count - 1)
                {
                    btScalar delta[3];                                          // delta in entity local coordinate system!
                    delta[0] = (btScalar)(*++pointer);                          // x = x;
                    delta[2] =-(btScalar)(*++pointer);                          // z =-y
                    delta[1] = (btScalar)(*++pointer);                          // y = z
                    tr[0] = entity->transform[0 + 0] * delta[0] + entity->transform[4 + 0] * delta[1] + entity->transform[8 + 0] * delta[2];
                    tr[1] = entity->transform[0 + 1] * delta[0] + entity->transform[4 + 1] * delta[1] + entity->transform[8 + 1] * delta[2];
                    tr[2] = entity->transform[0 + 2] * delta[0] + entity->transform[4 + 2] * delta[1] + entity->transform[8 + 2] * delta[2];
                }
                else
                {
                    pointer += 3;                                               // Parse through 3 operands.
                }
                break;
                
            case TR_ANIMCOMMAND_JUMPDISTANCE:
                pointer += 2;                                                   // Parse through 2 operands.
                break;
                
            case TR_ANIMCOMMAND_EMPTYHANDS:
                break;
                
            case TR_ANIMCOMMAND_KILL:
                break;
                
            case TR_ANIMCOMMAND_PLAYSOUND:
                ++pointer;
                break;
                
            case TR_ANIMCOMMAND_PLAYEFFECT:
                pointer += 2;                                                   // Parse through 2 operands.
                break;
        }
    }
}


void Entity_UpdateCurrentBoneFrame(entity_p entity)
{
    long int k, stack_use;
    btScalar cmd_tr[3];
    ss_bone_tag_p btag = entity->bf.bone_tags;
    bone_tag_p src_btag, next_btag;
    btScalar *stack, *sp, t;
    skeletal_model_p model = entity->model;
    bone_frame_p bf = model->animations[entity->current_animation].frames + entity->current_frame;    
        
    if(entity->next_bf == NULL)
    {
        entity->lerp = 0.0;
        entity->next_bf = bf;
    }

    t = 1.0 - entity->lerp;
    vec3_mul_scalar(cmd_tr, entity->next_bf_tr, entity->lerp);

    vec3_interpolate_macro(entity->bf.bb_max, bf->bb_max, entity->next_bf->bb_max, entity->lerp, t);
    vec3_add(entity->bf.bb_max, entity->bf.bb_max, cmd_tr);
    vec3_interpolate_macro(entity->bf.bb_min, bf->bb_min, entity->next_bf->bb_min, entity->lerp, t);
    vec3_add(entity->bf.bb_min, entity->bf.bb_min, cmd_tr);
    vec3_interpolate_macro(entity->bf.centre, bf->centre, entity->next_bf->centre, entity->lerp, t);
    vec3_add(entity->bf.centre, entity->bf.centre, cmd_tr);
    
    vec3_interpolate_macro(entity->bf.pos, bf->pos, entity->next_bf->pos, entity->lerp, t);
    vec3_add(entity->bf.pos, entity->bf.pos, cmd_tr);
    next_btag = entity->next_bf->bone_tags;
    src_btag = bf->bone_tags;
    for(k=0;k<bf->bone_tag_count;k++,btag++,src_btag++,next_btag++)
    {
        vec3_interpolate_macro(btag->offset, src_btag->offset, next_btag->offset, entity->lerp, t);      
        vec3_copy(btag->transform+12, btag->offset);
        btag->transform[15] = 1.0;
        if(k == 0)
        {
            vec3_add(btag->transform+12, btag->transform+12, entity->bf.pos);
        }
        
        vec4_slerp(btag->qrotate, src_btag->qrotate, next_btag->qrotate, entity->lerp);
        Mat4_set_qrotation(btag->transform, btag->qrotate);
    }

    /*
     * build absolute coordinate matrix system
     */
    sp = stack = GetTempbtScalar(model->mesh_count * 16);
    stack_use = 0;

    btag = entity->bf.bone_tags;

    Mat4_Copy(btag->full_transform, btag->transform);
    Mat4_Copy(sp, btag->transform);
    btag++;
    
    for(k=1;k<bf->bone_tag_count;k++,btag++)
    {
        if(btag->flag & 0x01)
        {
            if(stack_use > 0)
            {
                sp -= 16;// glPopMatrix();
                stack_use--;
            }
        }
        if(btag->flag & 0x02)
        {
            if(stack_use < model->mesh_count - 1)
            {
                Mat4_Copy(sp+16, sp);
                sp += 16;// glPushMatrix();
                stack_use++;
            }
        }
        Mat4_Mat4_mul(sp, sp, btag->transform); // glMultMatrixd(btag->transform);
        Mat4_Copy(btag->full_transform, sp);
    }

    ReturnTempbtScalar(model->mesh_count * 16);
}


int  Entity_GetWaterState(entity_p entity)
{
    if((!entity) || (!entity->character))
    {
        return false;
    }
    
    if(!entity->character->height_info.water)
    {
        return ENTITY_WATER_NONE;
    }
    else if( entity->character->height_info.water &&
            (entity->character->height_info.water_level > entity->transform[12 + 2]) && 
            (entity->character->height_info.water_level < entity->transform[12 + 2] + entity->character->wade_depth) )
    {
        return ENTITY_WATER_SHALLOW;
    }
    else if( entity->character->height_info.water &&
            (entity->character->height_info.water_level > entity->transform[12 + 2] + entity->character->wade_depth) )
    {
        return ENTITY_WATER_WADE;
    }
    else
    {
        return ENTITY_WATER_SWIM;
    }
}

void Entity_DoAnimCommands(entity_p entity, int changing)
{
    if((engine_world.anim_commands_count == 0) || 
       (entity->model->animations[entity->current_animation].num_anim_commands > 255))
    {
        return;  // If no anim commands or current anim has more than 255 (according to TRosettaStone).
    }
        
    animation_frame_p af  = entity->model->animations + entity->current_animation;
    uint32_t count        = af->num_anim_commands;
    int16_t *pointer      = engine_world.anim_commands + af->anim_command;
    int8_t   random_value = 0;
    
    for(uint32_t i = 0; i < count; i++, pointer++)
    {
        switch(*pointer)
        {
            case TR_ANIMCOMMAND_SETPOSITION:
                // This command executes ONLY at the end of animation. 
                if(entity->current_frame == af->frames_count - 1)
                {
                    btScalar delta[3];                                          // delta in entity local coordinate system!
                    delta[0] = (btScalar)(*++pointer);                          // x = x;
                    delta[2] =-(btScalar)(*++pointer);                          // z =-y
                    delta[1] = (btScalar)(*++pointer);                          // y = z
                    entity->transform[12] += entity->transform[0 + 0] * delta[0] + entity->transform[4 + 0] * delta[1] + entity->transform[8 + 0] * delta[2];
                    entity->transform[13] += entity->transform[0 + 1] * delta[0] + entity->transform[4 + 1] * delta[1] + entity->transform[8 + 1] * delta[2];
                    entity->transform[14] += entity->transform[0 + 2] * delta[0] + entity->transform[4 + 2] * delta[1] + entity->transform[8 + 2] * delta[2];
 /*                 
                    delta[0] = entity->transform[12 + 0];
                    delta[1] = entity->transform[12 + 1];
                    delta[2] = entity->transform[12 + 2] + 0.5 * (entity->bf.bb_min[2] + entity->bf.bb_max[2]);
                    entity->self->room = Room_FindPosCogerrence(&engine_world, delta, entity->self->room);*/  
                }
                else
                {
                    pointer += 3; // Parse through 3 operands.
                }
                break;
                
            case TR_ANIMCOMMAND_JUMPDISTANCE:
                // This command executes ONLY at the end of animation. 
                if(entity->current_frame == af->frames_count - 1)
                {
                    int16_t v_Vertical   = *++pointer;
                    int16_t v_Horizontal = *++pointer;
                    
                    Character_SetToJump(entity, -v_Vertical, v_Horizontal);
                }
                else
                {
                    pointer += 2; // Parse through 2 operands.
                }
                break;
                
            case TR_ANIMCOMMAND_EMPTYHANDS:
                ///@FIXME: Behaviour is yet to be discovered.
                break;
                
            case TR_ANIMCOMMAND_KILL:
                // This command executes ONLY at the end of animation. 
                if(entity->current_frame == af->frames_count - 1)
                {
                    if(entity->character)
                    {
                        entity->character->cmd.kill = 1;
                    }
                }
                
                break;
                
            case TR_ANIMCOMMAND_PLAYSOUND:
                bool    surface_flag[2];
                int16_t sound_index;
                
                if(entity->current_frame == *++pointer)
                {
                    sound_index = *++pointer & 0x3FFF;
                    
                    if(*pointer & TR_ANIMCOMMAND_CONDITION_WATER)
                    {
                        if(Entity_GetWaterState(entity) == ENTITY_WATER_SHALLOW)
                            Audio_Send(sound_index, TR_AUDIO_EMITTER_ENTITY, entity->ID);
                    }
                    else if(*pointer & TR_ANIMCOMMAND_CONDITION_LAND)
                    {
                        if(Entity_GetWaterState(entity) != ENTITY_WATER_SHALLOW)
                            Audio_Send(sound_index, TR_AUDIO_EMITTER_ENTITY, entity->ID);
                    }
                    else
                    {
                        Audio_Send(sound_index, TR_AUDIO_EMITTER_ENTITY, entity->ID);
                    }
                        
                }
                else
                {
                    pointer++;
                }
                break;
                
            case TR_ANIMCOMMAND_PLAYEFFECT:
                // Effects (flipeffects) are various non-typical actions which vary
                // across different TR game engine versions. There are common ones,
                // however, and currently only these are supported.
                if(entity->current_frame == *++pointer)
                {
                    switch(*++pointer & 0x3FFF)
                    {
                        case TR_EFFECT_CHANGEDIRECTION:
                            entity->angles[0] += 180.0;
                            if(entity->dir_flag == ENT_MOVE_BACKWARD)
                            {
                                entity->dir_flag = ENT_MOVE_FORWARD;
                            }
                            else if(entity->dir_flag == ENT_MOVE_FORWARD)
                            {
                                entity->dir_flag = ENT_MOVE_BACKWARD;
                            }
                            break;
                            
                        case TR_EFFECT_HIDEOBJECT:
                            entity->hide = 1;
                            break;
                            
                        case TR_EFFECT_SHOWOBJECT:
                            entity->hide = 0;
                            break;
                            
                        case TR_EFFECT_PLAYSTEPSOUND:
                            // Please note that we bypass land/water mask, as TR3-5 tends to ignore
                            // this flag and play step sound in any case on land, ignoring it
                            // completely in water rooms.
                            if(!Entity_GetWaterState(entity))
                            {
                                // TR3-5 footstep map.
                                // We define it here as a magic numbers array, because TR3-5 versions
                                // fortunately have no differences in footstep sounds order.
                                // Also note that some footstep types mutually share same sound IDs
                                // across different TR versions.
                                switch(entity->current_sector->box_index & 0x0F)
                                {
                                    case 0:                                     // Mud
                                        Audio_Send(288, TR_AUDIO_EMITTER_ENTITY, entity->ID);
                                        break;
                                        
                                    case 1:                                     // Snow - TR3 & TR5 only
                                        Audio_Send(293, TR_AUDIO_EMITTER_ENTITY, entity->ID);
                                        break;
                                        
                                    case 2:                                     // Sand - same as grass
                                        Audio_Send(291, TR_AUDIO_EMITTER_ENTITY, entity->ID);
                                        break;
                                        
                                    case 3:                                     // Gravel
                                        Audio_Send(290, TR_AUDIO_EMITTER_ENTITY, entity->ID);
                                        break;
                                        
                                    case 4:                                     // Ice - TR3 & TR5 only
                                        Audio_Send(289, TR_AUDIO_EMITTER_ENTITY, entity->ID);
                                        break;
                                        
                                    case 5:                                     // Water
                                        Audio_Send(17, TR_AUDIO_EMITTER_ENTITY, entity->ID);
                                        break;
                                        
                                    case 6:                                     // Stone - DEFAULT SOUND, BYPASS!
                                        Audio_Send(-1, TR_AUDIO_EMITTER_ENTITY, entity->ID);
                                        break;
                                        
                                    case 7:                                     // Wood
                                        Audio_Send(292, TR_AUDIO_EMITTER_ENTITY, entity->ID);
                                        break;
                                        
                                    case 8:                                     // Metal
                                        Audio_Send(294, TR_AUDIO_EMITTER_ENTITY, entity->ID);
                                        break;
                                        
                                    case 9:                                     // Marble - TR4 only
                                        Audio_Send(293, TR_AUDIO_EMITTER_ENTITY, entity->ID);
                                        break;
                                        
                                    case 10:                                    // Grass - same as sand
                                        Audio_Send(291, TR_AUDIO_EMITTER_ENTITY, entity->ID);
                                        break;
                                        
                                    case 11:                                    // Concrete - DEFAULT SOUND, BYPASS!
                                        Audio_Send(-1, TR_AUDIO_EMITTER_ENTITY, entity->ID);
                                        break;
                                        
                                    case 12:                                    // Old wood - same as wood 
                                        Audio_Send(292, TR_AUDIO_EMITTER_ENTITY, entity->ID);
                                        break;
                                        
                                    case 13:                                    // Old metal - same as metal
                                        Audio_Send(294, TR_AUDIO_EMITTER_ENTITY, entity->ID);
                                        break;
                                }
                            }
                            break;
                            
                        case TR_EFFECT_BUBBLE:
                            ///@FIXME: Spawn bubble particle here, when particle system is developed.
                            random_value = rand() % 100;
                            if(random_value > 60)
                            {
                                Audio_Send(37, TR_AUDIO_EMITTER_ENTITY, entity->ID);
                            }
                            break;
                            
                        default:
                            ///@FIXME: TODO ALL OTHER EFFECTS!
                            break;
                    }
                }
                else
                {
                    pointer++;
                }
                break;
        }
    }
}


int Entity_ParseFloorData(struct entity_s *ent, struct world_s *world)
{
    uint16_t function, sub_function, b3, FD_function, operands;
    uint16_t slope_t13, slope_t12, slope_t11, slope_t10, slope_func;
    int16_t slope_t01, slope_t00;
    int i, ret = 0;
    uint16_t *entry, *end_p, end_bit, cont_bit;
    room_sector_p sector = ent->current_sector;
    
    // Trigger options.
    
    bool   only_once;
    bool   trigger_mask[5];
    int8_t timer_field;
    
    if(ent->character)
    {
        ent->character->height_info.walls_climb = 0;
        ent->character->height_info.walls_climb_dir = 0;
        ent->character->height_info.ceiling_climb = 0;
    }
    
    if(!sector || (sector->fd_index <= 0) || (sector->fd_index >= world->floor_data_size))
    {
        return 0;
    }

    /*
     * PARSE FUNCTIONS
     */
    end_p = world->floor_data + world->floor_data_size - 1;
    entry = world->floor_data + sector->fd_index;

    do
    {
        end_bit = ((*entry) & 0x8000) >> 15;            // 0b10000000 00000000
        
        // TR_I - TR_II
        //function = (*entry) & 0x00FF;                   // 0b00000000 11111111
        //sub_function = ((*entry) & 0x7F00) >> 8;        // 0b01111111 00000000
        
        //TR_III+, but works with TR_I - TR_II
        function = (*entry) & 0x001F;                   // 0b00000000 00011111
        sub_function = ((*entry) & 0x3FF0) >> 8;        // 0b01111111 11100000
        b3 = ((*entry) & 0x00E0) >> 5;                  // 0b00000000 11100000  TR_III+
        
        entry++;

        switch(function)
        {
            case TR_FD_FUNC_PORTALSECTOR:          // PORTAL DATA
                if(sub_function == 0x00)
                {
                    i = *(entry++);
                    
                }
                break;

            case TR_FD_FUNC_FLOORSLANT:          // FLOOR SLANT
                if(sub_function == 0x00)
                {

                    entry++;
                }
                break;

            case TR_FD_FUNC_CEILINGSLANT:          // CEILING SLANT
                if(sub_function == 0x00)
                {

                    entry++;
                }
                break;

            case TR_FD_FUNC_TRIGGER:          // TRIGGER
                timer_field     = (*entry) & 0x00FF;
                only_once       = (*entry) & 0x0100;
                trigger_mask[0] = (*entry) & 0x0200;
                trigger_mask[1] = (*entry) & 0x0400;
                trigger_mask[2] = (*entry) & 0x0800;
                trigger_mask[3] = (*entry) & 0x1000;
                trigger_mask[4] = (*entry) & 0x2000;
                
                Con_Printf("TRIGGER: timer - %d, once - %d, mask - %d%d%d%d%d", timer_field, only_once, trigger_mask[0], trigger_mask[1], trigger_mask[2], trigger_mask[3], trigger_mask[4]);
                
                switch(sub_function)
                {
                    case TR_FD_TRIGTYPE_TRIGGER:
                        Con_Printf("TRIGGER TYPE: TR_FD_TRIGTYPE_TRIGGER");
                        break;
                    case TR_FD_TRIGTYPE_PAD:
                        Con_Printf("TRIGGER TYPE: TR_FD_TRIGTYPE_PAD");
                        break;
                    case TR_FD_TRIGTYPE_SWITCH:
                        Con_Printf("TRIGGER TYPE: TR_FD_TRIGTYPE_SWITCH");
                        break;
                    case TR_FD_TRIGTYPE_KEY:
                        Con_Printf("TRIGGER TYPE: TR_FD_TRIGTYPE_KEY");
                        break;
                    case TR_FD_TRIGTYPE_PICKUP:
                        Con_Printf("TRIGGER TYPE: TR_FD_TRIGTYPE_PICKUP");
                        break;
                    case TR_FD_TRIGTYPE_HEAVY:
                        Con_Printf("TRIGGER TYPE: TR_FD_TRIGTYPE_HEAVY");
                        break;
                    case TR_FD_TRIGTYPE_ANTIPAD:
                        Con_Printf("TRIGGER TYPE: TR_FD_TRIGTYPE_ANTIPAD");
                        break;
                    case TR_FD_TRIGTYPE_COMBAT:
                        Con_Printf("TRIGGER TYPE: TR_FD_TRIGTYPE_COMBAT");
                        break;
                    case TR_FD_TRIGTYPE_DUMMY:
                        Con_Printf("TRIGGER TYPE: TR_FD_TRIGTYPE_DUMMY");
                        break;
                    case TR_FD_TRIGTYPE_ANTITRIGGER:
                        Con_Printf("TRIGGER TYPE: TR_FD_TRIGTYPE_ANTITRIGGER");
                        break;
                    case TR_FD_TRIGTYPE_HEAVYTRIGGER:
                        Con_Printf("TRIGGER TYPE: TR_FD_TRIGTYPE_HEAVYTRIGGER");
                        break;
                    case TR_FD_TRIGTYPE_HEAVYANTITRIGGER:
                        Con_Printf("TRIGGER TYPE: TR_FD_TRIGTYPE_HEAVYANTITRIGGER");
                        break;
                    case TR_FD_TRIGTYPE_MONKEY:
                        Con_Printf("TRIGGER TYPE: TR_FD_TRIGTYPE_MONKEY");
                        break;
                    case TR_FD_TRIGTYPE_SKELETON:
                        Con_Printf("TRIGGER TYPE: TR_FD_TRIGTYPE_SKELETON");
                        break;
                    case TR_FD_TRIGTYPE_TIGHTROPE:
                        Con_Printf("TRIGGER TYPE: TR_FD_TRIGTYPE_TIGHTROPE");
                        break;
                    case TR_FD_TRIGTYPE_CRAWLDUCK:
                        Con_Printf("TRIGGER TYPE: TR_FD_TRIGTYPE_CRAWLDUCK");
                        break;
                    case TR_FD_TRIGTYPE_CLIMB:
                        Con_Printf("TRIGGER TYPE: TR_FD_TRIGTYPE_CLIMB");
                        break;
                }
                
                do
                {
                    entry++;
                    cont_bit = ((*entry) & 0x8000) >> 15;                       // 0b10000000 00000000
                    FD_function = (((*entry) & 0x7C00)) >> 10;                  // 0b01111100 00000000
                    operands = (*entry) & 0x03FF;                               // 0b00000011 11111111

                    switch(FD_function)
                    {
                        case TR_FD_TRIGFUNC_OBJECT:          // ACTIVATE / DEACTIVATE item
                            Con_Printf("Activate %d item", operands);
                            break;

                        case TR_FD_TRIGFUNC_CAMERATARGET:          // CAMERA SWITCH
                            {                                
                                uint8_t cam_index = (*entry) & 0x007F;
                                entry++;
                                uint8_t cam_timer = ((*entry) & 0x00FF);
                                uint8_t cam_once  = ((*entry) & 0x0100) >> 8;
                                uint8_t cam_zoom  = ((*entry) & 0x1000) >> 12;
                                        cont_bit  = ((*entry) & 0x8000) >> 15;                       // 0b10000000 00000000
                                
                                Con_Printf("CAMERA: index = %d, timer = %d, once = %d, zoom = %d", cam_index, cam_timer, cam_once, cam_zoom);
                            }
                            break;

                        case TR_FD_TRIGFUNC_UWCURRENT:          // UNDERWATER CURRENT
                            Con_Printf("UNDERWATER CURRENT! OP = %d", operands);
                            break;

                        case TR_FD_TRIGFUNC_FLIPMAP:          // SET ALTERNATE ROOM
                            Con_Printf("SET ALTERNATE ROOM! OP = %d", operands);
                            break;

                        case TR_FD_TRIGFUNC_FLIPON:          // ALTER ROOM FLAGS (paired with 0x05)
                            Con_Printf("ALTER ROOM FLAGS 0x04! OP = %d", operands);
                            break;

                        case TR_FD_TRIGFUNC_FLIPOFF:          // ALTER ROOM FLAGS (paired with 0x04)
                            Con_Printf("ALTER ROOM FLAGS 0x05! OP = %d", operands);
                            break;

                        case TR_FD_TRIGFUNC_LOOKAT:          // LOOK AT ITEM
                            Con_Printf("Look at %d item", operands);
                            break;

                        case TR_FD_TRIGFUNC_ENDLEVEL:          // END LEVEL
                            Con_Printf("End of level! id = %d", operands);
                            break;

                        case TR_FD_TRIGFUNC_PLAYTRACK:          // PLAY CD TRACK
                            Con_Printf("Play audiotrack id = %d", operands);
                            // operands - track number
                            break;

                        case TR_FD_TRIGFUNC_FLIPEFFECT:          // Various in-game actions.
                            Con_Printf("Flipeffect id = %d", operands);
                            break;

                        case TR_FD_TRIGFUNC_SECRET:          // PLAYSOUND SECRET_FOUND
                            Con_Printf("Play SECRET[%d] FOUND", operands);
                            break;

                        case TR_FD_TRIGFUNC_BODYBAG:          // UNKNOWN
                            Con_Printf("BODYBAG id = %d", operands);
                            break;

                        case TR_FD_TRIGFUNC_FLYBY:          // TR4-5: FLYBY CAMERA
                            Con_Printf("Flyby camera = %d", operands);
                            break;

                        case TR_FD_TRIGFUNC_CUTSCENE:          // USED IN TR4-5
                            Con_Printf("CUTSCENE id = %d", operands);
                            break;

                        case 0x0e:          // UNKNOWN
                            Con_Printf("TRIGGER: unknown 0x0e, OP = %d", operands);
                            break;

                        case 0x0f:          // UNKNOWN
                            Con_Printf("TRIGGER: unknown 0x0f, OP = %d", operands);
                            break;
                    };
                }
                while(!cont_bit && entry < end_p);
                break;

            case TR_FD_FUNC_DEATH:          // KILL LARA
                Con_Printf("KILL! sub = %d, b3 = %d", sub_function, b3);
                break;

            case TR_FD_FUNC_CLIMB:          // CLIMBABLE WALLS
                Con_Printf("Climbable walls! sub = %d, b3 = %d", sub_function, b3);
                if(ent->character)
                {
                    ent->character->height_info.walls_climb = 1;
                    ent->character->height_info.walls_climb_dir = sub_function;
                }
                break;

            case TR_FD_FUNC_SLOPE1:           // TR3 SLANT
            case TR_FD_FUNC_SLOPE2:           // TR3 SLANT
            case TR_FD_FUNC_SLOPE3:           // TR3 SLANT
            case TR_FD_FUNC_SLOPE4:           // TR3 SLANT
            case TR_FD_FUNC_SLOPE5:           // TR3 SLANT
            case TR_FD_FUNC_SLOPE6:           // TR3 SLANT
            case TR_FD_FUNC_SLOPE7:           // TR3 SLANT
            case TR_FD_FUNC_SLOPE8:           // TR3 SLANT
            case TR_FD_FUNC_SLOPE9:           // TR3 SLANT
            case TR_FD_FUNC_SLOPE10:          // TR3 SLANT
            case TR_FD_FUNC_SLOPE11:          // TR3 SLANT
            case TR_FD_FUNC_SLOPE12:          // TR3 SLANT
                cont_bit = ((*entry) & 0x8000) >> 15;       // 0b10000000 00000000
                slope_t01 = ((*entry) & 0x7C00) >> 10;      // 0b01111100 00000000
                slope_t00 = ((*entry) & 0x03E0) >> 5;       // 0b00000011 11100000
                slope_func = ((*entry) & 0x001F);           // 0b00000000 00011111
                entry++;
                slope_t13 = ((*entry) & 0xF000) >> 12;      // 0b11110000 00000000
                slope_t12 = ((*entry) & 0x0F00) >> 8;       // 0b00001111 00000000
                slope_t11 = ((*entry) & 0x00F0) >> 4;       // 0b00000000 11110000
                slope_t10 = ((*entry) & 0x000F);            // 0b00000000 00001111
                break;

            case TR_FD_FUNC_MONKEY:          // Climbable ceiling
                Con_Printf("Climbable ceiling! sub = %d, b3 = %d", sub_function, b3);
                if(ent->character)
                {
                    ent->character->height_info.ceiling_climb = 1;
                }
                if(sub_function == 0x00)
                {
                
                }
                break;
                
            case TR_FD_FUNC_TRIGGERER_MARK:
                Con_Printf("Trigger Triggerer (TR4) / MINECART LEFT (TR3), OP = %d", operands);
                break;
                
            case TR_FD_FUNC_BEETLE_MARK:
                Con_Printf("Clockwork Beetle mark (TR4) / MINECART RIGHT (TR3), OP = %d", operands);
                break;
                
            default:
                Con_Printf("UNKNOWN function id = %d, sub = %d, b3 = %d", function, sub_function, b3);
                break;
        };
        ret++;
    }
    while(!end_bit && entry < end_p);

    return ret;
}


void Entity_SetAnimation(entity_p entity, int animation, int frame)
{
    animation_frame_p anim;
    long int t;
    btScalar dt;
    
    if(!entity || !entity->model)
    {
        return;
    }

    if(animation < 0 || animation >= entity->model->animation_count)
    {
        animation = 0;
    }
    
    entity->next_bf == NULL;
    entity->lerp = 0.0;
    anim = &entity->model->animations[animation];
    frame %= anim->frames_count;
    frame = (frame >= 0)?(frame):(anim->frames_count - 1 + frame);
    entity->period = 1.0 / 30.0;
    
    entity->current_stateID = anim->state_id;
    entity->current_animation = animation;
    entity->current_speed = anim->speed;
    entity->current_frame = frame;
    
    entity->frame_time = (btScalar)frame * entity->period;
    t = (entity->frame_time) / entity->period;
    dt = entity->frame_time - (btScalar)t * entity->period;
    entity->frame_time = (btScalar)frame * entity->period + dt;
    
    Entity_UpdateCurrentBoneFrame(entity);
    Entity_UpdateRigidBody(entity);
}
    

struct state_change_s *Anim_FindStateChangeByAnim(struct animation_frame_s *anim, int state_change_anim)
{
    int i, j;
    state_change_p ret = anim->state_change;
    
    if(state_change_anim < 0)
    {
        return NULL;
    }
    
    for(i=0;i<anim->state_change_count;i++,ret++)
    {
        for(j=0;j<ret->anim_dispath_count;j++)
        {
            if(ret->anim_dispath[j].next_anim == state_change_anim)
            {
                return ret;
            }
        }
    }
    
    return NULL;
}


struct state_change_s *Anim_FindStateChangeByID(struct animation_frame_s *anim, int id)
{
    int i;
    state_change_p ret = anim->state_change;
    
    if(id < 0)
    {
        return NULL;
    }
    
    for(i=0;i<anim->state_change_count;i++,ret++)
    {
        if(ret->ID == id)
        {
            return ret;
        }
    }
    
    return NULL;
}


int Entity_GetAnimDispatchCase(struct entity_s *ent, int id)
{
    int i, j;
    animation_frame_p anim = ent->model->animations + ent->current_animation;
    state_change_p stc = anim->state_change;
    anim_dispath_p disp;
    
    if(id < 0)
    {
        return -1;
    }
    
    for(i=0;i<anim->state_change_count;i++,stc++)
    {
        if(stc->ID == id)
        {
            disp = stc->anim_dispath;
            for(j=0;j<stc->anim_dispath_count;j++,disp++)
            {
                if((disp->frame_high >= disp->frame_low) && (ent->current_frame >= disp->frame_low) && (ent->current_frame <= disp->frame_high))// ||
                   //(disp->frame_high <  disp->frame_low) && ((ent->current_frame >= disp->frame_low) || (ent->current_frame <= disp->frame_high)))
                {
                    return j;
                }
            }
        }
    }
    
    return -1;
}

/*
 * Next frame and next anim calculation function. 
 */
void Entity_GetNextFrame(const entity_p entity, btScalar time, struct state_change_s *stc, int *frame, int *anim)
{
    animation_frame_p curr_anim = entity->model->animations + entity->current_animation;
    anim_dispath_p disp;
    int i;

    *frame = (entity->frame_time + time) / entity->period;
    *frame = (*frame >= 0.0)?(*frame):(0.0);                                    // paranoid checking
    *anim = entity->current_animation;
    
    /*
     * Flag has a highest priority 
     */
    if(entity->anim_flags == ANIM_LOOP_LAST_FRAME)
    {
        if(*frame >= curr_anim->frames_count - 1)
        {
            *frame = curr_anim->frames_count - 1;
            *anim = entity->current_animation;                                  // paranoid dublicate
        }
        return;
    }
    
    /*
     * Check next anim if frame >= frames_count
     */
    if(*frame >= curr_anim->frames_count)
    {            
        if(curr_anim->next_anim)
        {
            *frame = curr_anim->next_frame;
            *anim = curr_anim->next_anim->ID;
            return;
        }
        
        *frame %= curr_anim->frames_count;
        *anim = entity->current_animation;                                      // paranoid dublicate
        return;
    }
    
    /*
     * State change check
     */
    if(stc)
    {
        disp = stc->anim_dispath;
        for(i=0;i<stc->anim_dispath_count;i++,disp++)
        {
            if((disp->frame_high >= disp->frame_low) && (*frame >= disp->frame_low) && (*frame <= disp->frame_high))// ||
               //(disp->frame_high <  disp->frame_low) && ((*frame >= disp->frame_low) || (*frame <= disp->frame_high)))
            {
                *anim = disp->next_anim;
                *frame = disp->next_frame;
                //*frame = (disp->next_frame + (*frame - disp->frame_low)) % entity->model->animations[disp->next_anim].frames_count;
                return;                                                         // anim was changed
            }
        }
    }
}


/**
 *
 */
int Entity_Frame(entity_p entity, btScalar time, int state_id)
{
    int frame, anim, ret = 0;
    long int t;
    btScalar dt;
    animation_frame_p af;
    state_change_p stc;
    
    vec4_set_zero(entity->next_bf_tr);
    if(!entity || !entity->active || !entity->model || !entity->model->animations || ((entity->model->animations->frames_count == 1) && (entity->model->animation_count == 1)))
    {
        return 0;
    }

    entity->next_bf = NULL;
    entity->lerp = 0.0;
    stc = Anim_FindStateChangeByID(entity->model->animations + entity->current_animation, state_id);
    Entity_GetNextFrame(entity, time, stc, &frame, &anim);
    if(anim != entity->current_animation)
    {
        ret = 2;
        Entity_DoAnimCommands(entity, ret);
        Entity_SetAnimation(entity, anim, frame);
        stc = NULL;
    }
    else if(entity->current_frame != frame)
    {
        ret = 1;
        Entity_DoAnimCommands(entity, ret);
        entity->current_frame = frame;
    }
    
    af = entity->model->animations + entity->current_animation;
    entity->frame_time += time;
    
    t = (entity->frame_time) / entity->period;
    dt = entity->frame_time - (btScalar)t * entity->period;
    entity->frame_time = (btScalar)frame * entity->period + dt;
    entity->lerp = (entity->smooth_anim)?(dt / entity->period):(0.0);
    
    Entity_GetNextFrame(entity, entity->period, stc, &frame, &anim);
    entity->next_bf = entity->model->animations[anim].frames + frame;
    Entity_GetAnimCommandTransform(entity, entity->current_animation, entity->current_frame, entity->next_bf_tr);

    /*
     * Update acceleration
     */
    if(entity->character)
    {
        entity->current_speed += time * entity->character->speed_mult * (btScalar)af->accel_hi;
    }
    
    Entity_UpdateCurrentBoneFrame(entity);
    Entity_UpdateRigidBody(entity);
    if(ret)
    {
        Entity_RebuildBV(entity);
    }
    return ret;
}

/**
 * The function rebuild / renew entity's BV
 */
void Entity_RebuildBV(entity_p ent)
{
    if(!ent || !ent->model)
    {
        return;
    }

    /*
     * get current BB from animation
     */
    switch(ent->bv->bv_type)
    {
        case BV_CYLINDER:
            BV_TransformZZ(ent->bv, ent->bf.bb_min[2] + ent->bv->r, ent->bf.bb_max[2]);
            return;

        case BV_BOX:
            BV_RebuildBox(ent->bv, ent->bf.bb_min, ent->bf.bb_max);
            BV_Transform(ent->bv);
            return;

        case BV_SPHERE:
            Mat4_vec3_mul_macro(ent->bv->centre, ent->bv->transform, ent->bv->base_centre);
            return;

        case BV_FREEFORM:
            BV_Transform(ent->bv);
            return;

        case BV_EMPTY:
        default:
            return;
    };
}


void Entity_MoveForward(struct entity_s *ent, btScalar dist)
{
    ent->transform[12] += ent->transform[4] * dist;
    ent->transform[13] += ent->transform[5] * dist;
    ent->transform[14] += ent->transform[6] * dist;
}


void Entity_MoveStrafe(struct entity_s *ent, btScalar dist)
{
    ent->transform[12] += ent->transform[0] * dist;
    ent->transform[13] += ent->transform[1] * dist;
    ent->transform[14] += ent->transform[2] * dist;
}


void Entity_MoveVertical(struct entity_s *ent, btScalar dist)
{
    ent->transform[12] += ent->transform[8] * dist;
    ent->transform[13] += ent->transform[9] * dist;
    ent->transform[14] += ent->transform[10] * dist;
}
