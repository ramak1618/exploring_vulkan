enum camera_movement {
    MOVEMENT_FORWARD, MOVEMENT_LEFT, MOVEMENT_RIGHT, MOVEMENT_BACKWARD, MOVEMENT_UP, MOVEMENT_DOWN,
    MOVEMENT_TURNLEFT, MOVEMENT_TURNRIGHT, MOVEMENT_LOOKUP, MOVEMENT_LOOKDOWN, MOVEMENT_ROLLLEFT, MOVEMENT_ROLLRIGHT,
    MOVEMENT_NONE,
};
#define NUM_CAM_MOVES 12

enum camera_movement keymap(xkb_keysym_t sym) {
    switch(sym) {
        case XKB_KEY_w:
            return MOVEMENT_FORWARD;
        case XKB_KEY_a:
            return MOVEMENT_LEFT;
        case XKB_KEY_s:
            return MOVEMENT_BACKWARD;
        case XKB_KEY_d:
            return MOVEMENT_RIGHT;
        case XKB_KEY_q:
            return MOVEMENT_UP;
        case XKB_KEY_e:
            return MOVEMENT_DOWN;
        case XKB_KEY_j:
            return MOVEMENT_TURNLEFT;
        case XKB_KEY_l:
            return MOVEMENT_TURNRIGHT;
        case XKB_KEY_i:
            return MOVEMENT_LOOKUP;
        case XKB_KEY_k:
            return MOVEMENT_LOOKDOWN;
        case XKB_KEY_u:
            return MOVEMENT_ROLLLEFT;
        case XKB_KEY_o:
            return MOVEMENT_ROLLRIGHT;
        default:
            return MOVEMENT_NONE;
    }
}

struct camera {
    float pos[3];
    float right[3];
    float down[3];
    float forward[3];
};

/*
 *  An overview of the view matrix
 *  It looks like so:
 *   _       _
 *  | R R R 0 |
 *  | R R R 0 |
 *  | R R R 0 |
 *  |_V V V 1_|
 *
 *  Where the 3x3 R grid is a rotation matrix, specifying the rotation that the camera has done from initial state
 *  And the 1x3 V grid is a translation vector.
 *
 * The translation vector is given by:
 *     R V = -X
 *  ( here V is a 3x1 column vector the transpose of the 1x3 V on the view matrix )
 *  Where R is the rotation matrix (the 3x3 grid) and X is the position vector of camera.
 *
 * The idea is that the camera rotates first and then translates to a given position
 *
 * To arrive at this matrix try expressing the above idea, 
 * Find its inverse, because we actually rotate the rest of the world in the opposite direction
 * Find the inversed matrix's transpose, because GLSL matrices are column-major.
 *
 * In all the below functions you should take the intuition that camera is the one doing the movement.
 * ( Alternatively you can also break your head :) )
 *
 * Also remember global coords work as:
 * X right, Y down, Z forward
 */

void build_view_matrix(float* restrict viewmat, const struct camera* restrict cam) {
    for(uint32_t i=0; i<3; i++) {
        viewmat[4*i] = cam->right[i];
    }

    for(uint32_t i=0; i<3; i++) {
        viewmat[4*i + 1] = cam->down[i];
    }

    for(uint32_t i=0; i<3; i++) {
        viewmat[4*i + 2] = cam->forward[i];
    }

    // Translation Vector 
    viewmat[12] = -(   cam->right[0]*cam->pos[0] +   cam->right[1]*cam->pos[1] +   cam->right[2]*cam->pos[2] );
    viewmat[13] = -(    cam->down[0]*cam->pos[0] +    cam->down[1]*cam->pos[1] +    cam->down[2]*cam->pos[2] );
    viewmat[14] = -( cam->forward[0]*cam->pos[0] + cam->forward[1]*cam->pos[1] + cam->forward[2]*cam->pos[2] );

    // 4th column
    viewmat[3] = 0.f;
    viewmat[7] = 0.f;
    viewmat[11] = 0.f;
    viewmat[15] = 1.f;
}

void update_camera(struct camera* cam, enum camera_movement type, float velocity, float delta_time) {
    switch(type) {
        case MOVEMENT_FORWARD:
            cam->pos[0] += cam->forward[0] * velocity * delta_time;
            cam->pos[1] += cam->forward[1] * velocity * delta_time;
            cam->pos[2] += cam->forward[2] * velocity * delta_time;
            return;
        case MOVEMENT_BACKWARD:
            cam->pos[0] -= cam->forward[0] * velocity * delta_time;
            cam->pos[1] -= cam->forward[1] * velocity * delta_time;
            cam->pos[2] -= cam->forward[2] * velocity * delta_time;
            return;
        case MOVEMENT_RIGHT:
            cam->pos[0] += cam->right[0] * velocity * delta_time;
            cam->pos[1] += cam->right[1] * velocity * delta_time;
            cam->pos[2] += cam->right[2] * velocity * delta_time;
            return;
        case MOVEMENT_LEFT:
            cam->pos[0] -= cam->right[0] * velocity * delta_time;
            cam->pos[1] -= cam->right[1] * velocity * delta_time;
            cam->pos[2] -= cam->right[2] * velocity * delta_time;
            return;
        case MOVEMENT_UP:
            cam->pos[0] -= cam->down[0] * velocity * delta_time;
            cam->pos[1] -= cam->down[1] * velocity * delta_time;
            cam->pos[2] -= cam->down[2] * velocity * delta_time;
            return;
        case MOVEMENT_DOWN:
            cam->pos[0] += cam->down[0] * velocity * delta_time;
            cam->pos[1] += cam->down[1] * velocity * delta_time;
            cam->pos[2] += cam->down[2] * velocity * delta_time;
            return;

        case MOVEMENT_TURNLEFT: {
            float cos_wt = cosf(velocity * delta_time);
            float sin_wt = sinf(velocity * delta_time);
            for(uint32_t i=0; i<3; i++)
                cam->right[i] = cam->right[i] * cos_wt +  cam->forward[i] * sin_wt;
            
            // Preserves magnitude
            float mag = sqrtf(cam->right[0]*cam->right[0] + cam->right[1]*cam->right[1] + cam->right[2]*cam->right[2]);
            cam->right[0] /= mag;
            cam->right[1] /= mag;
            cam->right[2] /= mag;

            // Preserves Orthogonality
            cam->forward[0] = cam->right[1]*cam->down[2] - cam->right[2]*cam->down[1];
            cam->forward[1] = cam->right[2]*cam->down[0] - cam->right[0]*cam->down[2];
            cam->forward[2] = cam->right[0]*cam->down[1] - cam->right[1]*cam->down[0];

            return;
        }
        case MOVEMENT_TURNRIGHT: {
            float cos_wt = cosf(velocity * delta_time);
            float sin_wt = sinf(velocity * delta_time);
            for(uint32_t i=0; i<3; i++)
                cam->right[i] = cam->right[i] * cos_wt - cam->forward[i] * sin_wt;
            
            // Preserves magnitude
            float mag = sqrtf(cam->right[0]*cam->right[0] + cam->right[1]*cam->right[1] + cam->right[2]*cam->right[2]);
            cam->right[0] /= mag;
            cam->right[1] /= mag;
            cam->right[2] /= mag;

            // Preserves Orthogonality
            cam->forward[0] = cam->right[1]*cam->down[2] - cam->right[2]*cam->down[1];
            cam->forward[1] = cam->right[2]*cam->down[0] - cam->right[0]*cam->down[2];
            cam->forward[2] = cam->right[0]*cam->down[1] - cam->right[1]*cam->down[0];

            return;
        }

        case MOVEMENT_LOOKUP: {
            float cos_wt = cosf(velocity * delta_time);
            float sin_wt = sinf(velocity * delta_time);
            for(uint32_t i=0; i<3; i++)
                cam->forward[i] = cam->forward[i] * cos_wt - cam->down[i] * sin_wt;
            
            // Preserves magnitude
            float mag = sqrtf(cam->forward[0]*cam->forward[0] + cam->forward[1]*cam->forward[1] + cam->forward[2]*cam->forward[2]);
            cam->forward[0] /= mag;
            cam->forward[1] /= mag;
            cam->forward[2] /= mag;

            // Preserves Orthogonality
            cam->down[0] = cam->forward[1]*cam->right[2] - cam->forward[2]*cam->right[1];
            cam->down[1] = cam->forward[2]*cam->right[0] - cam->forward[0]*cam->right[2];
            cam->down[2] = cam->forward[0]*cam->right[1] - cam->forward[1]*cam->right[0];

            return;
        }

        case MOVEMENT_LOOKDOWN: {
            float cos_wt = cosf(velocity * delta_time);
            float sin_wt = sinf(velocity * delta_time);
            for(uint32_t i=0; i<3; i++)
                cam->forward[i] = cam->forward[i] * cos_wt + cam->down[i] * sin_wt;
            
            // Preserves magnitude
            float mag = sqrtf(cam->forward[0]*cam->forward[0] + cam->forward[1]*cam->forward[1] + cam->forward[2]*cam->forward[2]);
            cam->forward[0] /= mag;
            cam->forward[1] /= mag;
            cam->forward[2] /= mag;

            // Preserves Orthogonality
            cam->down[0] = cam->forward[1]*cam->right[2] - cam->forward[2]*cam->right[1];
            cam->down[1] = cam->forward[2]*cam->right[0] - cam->forward[0]*cam->right[2];
            cam->down[2] = cam->forward[0]*cam->right[1] - cam->forward[1]*cam->right[0];

            return;
        }

        case MOVEMENT_ROLLLEFT: {
            float cos_wt = cosf(velocity * delta_time);
            float sin_wt = sinf(velocity * delta_time);
            for(uint32_t i=0; i<3; i++)
                cam->down[i] = cam->down[i] * cos_wt + cam->right[i] * sin_wt;
            
            // Preserves magnitude
            float mag = sqrtf(cam->down[0]*cam->down[0] + cam->down[1]*cam->down[1] + cam->down[2]*cam->down[2]);
            cam->down[0] /= mag;
            cam->down[1] /= mag;
            cam->down[2] /= mag;

            // Preserves Orthogonality
            cam->right[0] = cam->down[1]*cam->forward[2] - cam->down[2]*cam->forward[1];
            cam->right[1] = cam->down[2]*cam->forward[0] - cam->down[0]*cam->forward[2];
            cam->right[2] = cam->down[0]*cam->forward[1] - cam->down[1]*cam->forward[0];

            return;
        }

        case MOVEMENT_ROLLRIGHT: {
            float cos_wt = cosf(velocity * delta_time);
            float sin_wt = sinf(velocity * delta_time);
            for(uint32_t i=0; i<3; i++)
                cam->down[i] = cam->down[i] * cos_wt - cam->right[i] * sin_wt;
            
            // Preserves magnitude
            float mag = sqrtf(cam->down[0]*cam->down[0] + cam->down[1]*cam->down[1] + cam->down[2]*cam->down[2]);
            cam->down[0] /= mag;
            cam->down[1] /= mag;
            cam->down[2] /= mag;

            // Preserves Orthogonality
            cam->right[0] = cam->down[1]*cam->forward[2] - cam->down[2]*cam->forward[1];
            cam->right[1] = cam->down[2]*cam->forward[0] - cam->down[0]*cam->forward[2];
            cam->right[2] = cam->down[0]*cam->forward[1] - cam->down[1]*cam->forward[0];

            return;
        }

        default:
            return;
    }
}
