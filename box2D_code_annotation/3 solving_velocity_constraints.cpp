


so continuing from article 2, we have now look at how he solves velocityConstraints

1.  first we just retrieve all the variables 
                
                b2ContactSolver.cpp


                void b2ContactSolver::SolveVelocityConstraints()
                {
                    for (int32 i = 0; i < m_count; ++i)
                    {
                        b2ContactVelocityConstraint* vc = m_velocityConstraints + i;

                        int32 indexA = vc->indexA;
                        int32 indexB = vc->indexB;
                        float32 mA = vc->invMassA;
                        float32 iA = vc->invIA;
                        float32 mB = vc->invMassB;
                        float32 iB = vc->invIB;
                        int32 pointCount = vc->pointCount;

                        b2Vec2 vA = m_velocities[indexA].v;
                        float32 wA = m_velocities[indexA].w;
                        b2Vec2 vB = m_velocities[indexB].v;
                        float32 wB = m_velocities[indexB].w;

                        b2Vec2 normal = vc->normal;
                        b2Vec2 tangent = b2Cross(normal, 1.0f);
                        float32 friction = vc->friction;

                        b2Assert(pointCount == 1 || pointCount == 2);



                        ...
                        ...
                    }
                }



2.  then we look at friction 

                        // Solve tangent constraints first because non-penetration is more important
                        // than friction.
                        for (int32 j = 0; j < pointCount; ++j)
                        {
                            b2VelocityConstraintPoint* vcp = vc->points + j;

                            // Relative velocity at contact
                            b2Vec2 dv = vB + b2Cross(wB, vcp->rB) - vA - b2Cross(wA, vcp->rA);

                            // Compute tangent force
                            float32 vt = b2Dot(dv, tangent) - vc->tangentSpeed;
                            float32 lambda = vcp->tangentMass * (-vt);

                            // b2Clamp the accumulated force
                            float32 maxFriction = friction * vcp->normalImpulse;
                            float32 newImpulse = b2Clamp(vcp->tangentImpulse + lambda, -maxFriction, maxFriction);
                            lambda = newImpulse - vcp->tangentImpulse;
                            vcp->tangentImpulse = newImpulse;

                            // Apply contact impulse
                            b2Vec2 P = lambda * tangent;

                            vA -= mA * P;
                            wA -= iA * b2Cross(vcp->rA, P);

                            vB += mB * P;
                            wB += iB * b2Cross(vcp->rB, P);
                        }



3.  now we look at normal constraints

                        // Solve normal constraints
                        if (pointCount == 1 || g_blockSolve == false)
                        {
                            for (int32 j = 0; j < pointCount; ++j)
                            {
                                b2VelocityConstraintPoint* vcp = vc->points + j;

                                // Relative velocity at contact
                                b2Vec2 dv = vB + b2Cross(wB, vcp->rB) - vA - b2Cross(wA, vcp->rA);

                                // Compute normal impulse
                                float32 vn = b2Dot(dv, normal);
                                float32 lambda = -vcp->normalMass * (vn - vcp->velocityBias);

                                // b2Clamp the accumulated impulse
                                float32 newImpulse = b2Max(vcp->normalImpulse + lambda, 0.0f);
                                lambda = newImpulse - vcp->normalImpulse;
                                vcp->normalImpulse = newImpulse;

                                // Apply contact impulse
                                b2Vec2 P = lambda * normal;
                                vA -= mA * P;
                                wA -= iA * b2Cross(vcp->rA, P);

                                vB += mB * P;
                                wB += iB * b2Cross(vcp->rB, P);
                            }
                        }
                        else
                        {
                            ########################################################
                            ########### logic for using the block solver ###########
                            ########################################################                            
                        }

                        m_velocities[indexA].v = vA;
                        m_velocities[indexA].w = wA;
                        m_velocities[indexB].v = vB;
                        m_velocities[indexB].w = wB;
                    }
                }







4.  we now look at the block solver 

                       // Solve normal constraints
                        if (pointCount == 1 || g_blockSolve == false)
                        {
                            for (int32 j = 0; j < pointCount; ++j)
                            {
                                b2VelocityConstraintPoint* vcp = vc->points + j;

                                // Relative velocity at contact
                                b2Vec2 dv = vB + b2Cross(wB, vcp->rB) - vA - b2Cross(wA, vcp->rA);

                                // Compute normal impulse
                                float32 vn = b2Dot(dv, normal);
                                float32 lambda = -vcp->normalMass * (vn - vcp->velocityBias);

                                // b2Clamp the accumulated impulse
                                float32 newImpulse = b2Max(vcp->normalImpulse + lambda, 0.0f);
                                lambda = newImpulse - vcp->normalImpulse;
                                vcp->normalImpulse = newImpulse;

                                // Apply contact impulse
                                b2Vec2 P = lambda * normal;
                                vA -= mA * P;
                                wA -= iA * b2Cross(vcp->rA, P);

                                vB += mB * P;
                                wB += iB * b2Cross(vcp->rB, P);
                            }
                        }
                        else
                        {
                            // Block solver developed in collaboration with Dirk Gregorius (back in 01/07 on Box2D_Lite).
                            // Build the mini LCP for this contact patch
                            //
                            // vn = A * x + b, vn >= 0, x >= 0 and vn_i * x_i = 0 with i = 1..2
                            //
                            // A = J * W * JT and J = ( -n, -r1 x n, n, r2 x n )
                            // b = vn0 - velocityBias
                            //
                            // The system is solved using the "Total enumeration method" (s. Murty). The complementary constraint vn_i * x_i
                            // implies that we must have in any solution either vn_i = 0 or x_i = 0. So for the 2D contact problem the cases
                            // vn1 = 0 and vn2 = 0, x1 = 0 and x2 = 0, x1 = 0 and vn2 = 0, x2 = 0 and vn1 = 0 need to be tested. The first valid
                            // solution that satisfies the problem is chosen.
                            // 
                            // In order to account of the accumulated impulse 'a' (because of the iterative nature of the solver which only requires
                            // that the accumulated impulse is clamped and not the incremental impulse) we change the impulse variable (x_i).
                            //
                            // Substitute:
                            // 
                            // x = a + d
                            // 
                            // a := old total impulse
                            // x := new total impulse
                            // d := incremental impulse 
                            //
                            // For the current iteration we extend the formula for the incremental impulse
                            // to compute the new total impulse:
                            //
                            // vn = A * d + b
                            //    = A * (x - a) + b
                            //    = A * x + b - A * a
                            //    = A * x + b'
                            // b' = b - A * a;

                            b2VelocityConstraintPoint* cp1 = vc->points + 0;
                            b2VelocityConstraintPoint* cp2 = vc->points + 1;

                            b2Vec2 a(cp1->normalImpulse, cp2->normalImpulse);
                            b2Assert(a.x >= 0.0f && a.y >= 0.0f);

                            // Relative velocity at contact
                            b2Vec2 dv1 = vB + b2Cross(wB, cp1->rB) - vA - b2Cross(wA, cp1->rA);
                            b2Vec2 dv2 = vB + b2Cross(wB, cp2->rB) - vA - b2Cross(wA, cp2->rA);

                            // Compute normal velocity
                            float32 vn1 = b2Dot(dv1, normal);
                            float32 vn2 = b2Dot(dv2, normal);

                            b2Vec2 b;
                            b.x = vn1 - cp1->velocityBias;
                            b.y = vn2 - cp2->velocityBias;

                            // Compute b'
                            b -= b2Mul(vc->K, a);

                            const float32 k_errorTol = 1e-3f;
                            B2_NOT_USED(k_errorTol);

                            for (;;)
                            {
                                //
                                // Case 1: vn = 0
                                //
                                // 0 = A * x + b'
                                //
                                // Solve for x:
                                //
                                // x = - inv(A) * b'
                                //
                                b2Vec2 x = - b2Mul(vc->normalMass, b);

                                if (x.x >= 0.0f && x.y >= 0.0f)
                                {
                                    // Get the incremental impulse
                                    b2Vec2 d = x - a;

                                    // Apply incremental impulse
                                    b2Vec2 P1 = d.x * normal;
                                    b2Vec2 P2 = d.y * normal;
                                    vA -= mA * (P1 + P2);
                                    wA -= iA * (b2Cross(cp1->rA, P1) + b2Cross(cp2->rA, P2));

                                    vB += mB * (P1 + P2);
                                    wB += iB * (b2Cross(cp1->rB, P1) + b2Cross(cp2->rB, P2));

                                    // Accumulate
                                    cp1->normalImpulse = x.x;
                                    cp2->normalImpulse = x.y;

                #if B2_DEBUG_SOLVER == 1
                                    // Postconditions
                                    dv1 = vB + b2Cross(wB, cp1->rB) - vA - b2Cross(wA, cp1->rA);
                                    dv2 = vB + b2Cross(wB, cp2->rB) - vA - b2Cross(wA, cp2->rA);

                                    // Compute normal velocity
                                    vn1 = b2Dot(dv1, normal);
                                    vn2 = b2Dot(dv2, normal);

                                    b2Assert(b2Abs(vn1 - cp1->velocityBias) < k_errorTol);
                                    b2Assert(b2Abs(vn2 - cp2->velocityBias) < k_errorTol);
                #endif
                                    break;
                                }

                                //
                                // Case 2: vn1 = 0 and x2 = 0
                                //
                                //   0 = a11 * x1 + a12 * 0 + b1' 
                                // vn2 = a21 * x1 + a22 * 0 + b2'
                                //
                                x.x = - cp1->normalMass * b.x;
                                x.y = 0.0f;
                                vn1 = 0.0f;
                                vn2 = vc->K.ex.y * x.x + b.y;
                                if (x.x >= 0.0f && vn2 >= 0.0f)
                                {
                                    // Get the incremental impulse
                                    b2Vec2 d = x - a;

                                    // Apply incremental impulse
                                    b2Vec2 P1 = d.x * normal;
                                    b2Vec2 P2 = d.y * normal;
                                    vA -= mA * (P1 + P2);
                                    wA -= iA * (b2Cross(cp1->rA, P1) + b2Cross(cp2->rA, P2));

                                    vB += mB * (P1 + P2);
                                    wB += iB * (b2Cross(cp1->rB, P1) + b2Cross(cp2->rB, P2));

                                    // Accumulate
                                    cp1->normalImpulse = x.x;
                                    cp2->normalImpulse = x.y;

                #if B2_DEBUG_SOLVER == 1
                                    // Postconditions
                                    dv1 = vB + b2Cross(wB, cp1->rB) - vA - b2Cross(wA, cp1->rA);

                                    // Compute normal velocity
                                    vn1 = b2Dot(dv1, normal);

                                    b2Assert(b2Abs(vn1 - cp1->velocityBias) < k_errorTol);
                #endif
                                    break;
                                }


                                //
                                // Case 3: vn2 = 0 and x1 = 0
                                //
                                // vn1 = a11 * 0 + a12 * x2 + b1' 
                                //   0 = a21 * 0 + a22 * x2 + b2'
                                //
                                x.x = 0.0f;
                                x.y = - cp2->normalMass * b.y;
                                vn1 = vc->K.ey.x * x.y + b.x;
                                vn2 = 0.0f;

                                if (x.y >= 0.0f && vn1 >= 0.0f)
                                {
                                    // Resubstitute for the incremental impulse
                                    b2Vec2 d = x - a;

                                    // Apply incremental impulse
                                    b2Vec2 P1 = d.x * normal;
                                    b2Vec2 P2 = d.y * normal;
                                    vA -= mA * (P1 + P2);
                                    wA -= iA * (b2Cross(cp1->rA, P1) + b2Cross(cp2->rA, P2));

                                    vB += mB * (P1 + P2);
                                    wB += iB * (b2Cross(cp1->rB, P1) + b2Cross(cp2->rB, P2));

                                    // Accumulate
                                    cp1->normalImpulse = x.x;
                                    cp2->normalImpulse = x.y;

                #if B2_DEBUG_SOLVER == 1
                                    // Postconditions
                                    dv2 = vB + b2Cross(wB, cp2->rB) - vA - b2Cross(wA, cp2->rA);

                                    // Compute normal velocity
                                    vn2 = b2Dot(dv2, normal);

                                    b2Assert(b2Abs(vn2 - cp2->velocityBias) < k_errorTol);
                #endif
                                    break;
                                }

                                //
                                // Case 4: x1 = 0 and x2 = 0
                                // 
                                // vn1 = b1
                                // vn2 = b2;
                                x.x = 0.0f;
                                x.y = 0.0f;
                                vn1 = b.x;
                                vn2 = b.y;

                                if (vn1 >= 0.0f && vn2 >= 0.0f )
                                {
                                    // Resubstitute for the incremental impulse
                                    b2Vec2 d = x - a;

                                    // Apply incremental impulse
                                    b2Vec2 P1 = d.x * normal;
                                    b2Vec2 P2 = d.y * normal;
                                    vA -= mA * (P1 + P2);
                                    wA -= iA * (b2Cross(cp1->rA, P1) + b2Cross(cp2->rA, P2));

                                    vB += mB * (P1 + P2);
                                    wB += iB * (b2Cross(cp1->rB, P1) + b2Cross(cp2->rB, P2));

                                    // Accumulate
                                    cp1->normalImpulse = x.x;
                                    cp2->normalImpulse = x.y;

                                    break;
                                }

                                // No solution, give up. This is hit sometimes, but it doesn't seem to matter.
                                break;
                            }
                        }

                        m_velocities[indexA].v = vA;
                        m_velocities[indexA].w = wA;
                        m_velocities[indexB].v = vB;
                        m_velocities[indexB].w = wB;
                    }
                }



