#include <incflo.H>

#ifdef AMREX_USE_EB
#include <AMReX_EB_utils.H>
#endif

using namespace amrex;

//
// Compute the three components of the convection term
//
void
incflo::incflo_compute_convective_term( Vector< std::unique_ptr<MultiFab> >& conv_u_in, 
                                        Vector< std::unique_ptr<MultiFab> >& conv_r_in,
                                        Vector< std::unique_ptr<MultiFab> >& conv_t_in,
                                        Vector< std::unique_ptr<MultiFab> >& vel_forces_in,
                                        Vector< std::unique_ptr<MultiFab> >& scal_forces_in,
                                        Vector< std::unique_ptr<MultiFab> >& vel_in,
                                        Vector< std::unique_ptr<MultiFab> >& density_in,
                                        Vector< std::unique_ptr<MultiFab> >& tracer_in,
                                        Real time)
{
    BL_PROFILE("incflo::incflo_compute_convective_term");

    // Temporaries to store fluxes 
    Vector< std::unique_ptr<MultiFab> > fx;
    Vector< std::unique_ptr<MultiFab> > fy;
    Vector< std::unique_ptr<MultiFab> > fz;

    fx.resize(finest_level+1);
    fy.resize(finest_level+1);
    fz.resize(finest_level+1);

    int num_comp;

    // First do FillPatch of {velocity, density, tracer} so we know the ghost cells of
    // these arrays are all filled
    for (int lev = 0; lev <= finest_level; lev++)
    {

#ifdef AMREX_USE_EB
    const FabFactory<FArrayBox>& factory =  *ebfactory[lev];
#else
    const FabFactory<FArrayBox>& factory = FArrayBoxFactory();
#endif

        // State with ghost cells
        MultiFab Sborder_u(grids[lev], dmap[lev], vel_in[lev]->nComp(), nghost, MFInfo(), factory);
        FillPatchVel(lev, time, Sborder_u);

        // Copy each FAB back from Sborder_u into the vel array, complete with filled ghost cells
        MultiFab::Copy (*vel_in[lev], Sborder_u, 0, 0, vel_in[lev]->nComp(), nghost);

        MultiFab Sborder_r(grids[lev], dmap[lev], 1, nghost, MFInfo(), factory);
        FillPatchDensity(lev, time, Sborder_r);
        MultiFab::Copy (*density_in[lev], Sborder_r, 0, 0, 1, nghost);

        if (advect_tracer)
        {
           MultiFab Sborder_s(grids[lev], dmap[lev], ntrac, nghost, MFInfo(), factory);
           FillPatchScalar(lev, time, Sborder_s);
           MultiFab::Copy (*tracer_in[lev], Sborder_s, 0, 0, tracer_in[lev]->nComp(), nghost);
        }
 
        // We need this to avoid FPE
        m_u_mac[lev]->setVal(covered_val);
        m_v_mac[lev]->setVal(covered_val);
        m_w_mac[lev]->setVal(covered_val);
 
        // Predict normal velocity to faces -- note that the {u_mac, v_mac, w_mac}
        //    arrays returned from this call are on face CENTROIDS
        if (use_godunov) 
           incflo_predict_godunov(lev, time, vel_in, vel_forces_in);
        else
           incflo_predict_vels_on_faces(lev, time, vel_in);
    }

    // Do projection on all AMR levels in one shot -- note that the {u_mac, v_mac, w_mac}
    //    arrays returned from this call are in on face CENTROIDS
    apply_MAC_projection (m_u_mac, m_v_mac, m_w_mac, density_in, time);

    int flux_ngrow = 2;

    for (int lev=0; lev <= finest_level; ++lev)
    {

#ifdef AMREX_USE_EB
    const FabFactory<FArrayBox>& factory =  *ebfactory[lev];
#else
    const FabFactory<FArrayBox>& factory = FArrayBoxFactory();
#endif
        if (advect_tracer)
        {
            // Convert tracer to (rho * tracer) so we can use conservative update
            for (int i = 0; i < ntrac; i++) 
               MultiFab::Multiply(*tracer_in[lev],*density_in[lev],0,i,1,tracer_in[lev]->nGrow());
        }

        // **************************************************
        // Compute div (u u) -- the update for velocity
        // **************************************************
        num_comp = 3; 
 
        fx[lev].reset(new MultiFab(m_u_mac[lev]->boxArray(),dmap[lev],num_comp,flux_ngrow,MFInfo(),factory));
        fy[lev].reset(new MultiFab(m_v_mac[lev]->boxArray(),dmap[lev],num_comp,flux_ngrow,MFInfo(),factory));
        fz[lev].reset(new MultiFab(m_w_mac[lev]->boxArray(),dmap[lev],num_comp,flux_ngrow,MFInfo(),factory));

        incflo_compute_fluxes(lev, fx, fy, fz, vel_in, 0, vel_forces_in, 0, num_comp,
                              xslopes_u, yslopes_u, zslopes_u, 0,
                              m_u_mac, m_v_mac, m_w_mac);

        incflo_divergence_plus_redist(lev, conv_u_in, fx, fy, fz, num_comp);

        // **************************************************
        // Compute div (rho u) -- the update for density
        // **************************************************
        conv_r_in[lev]->setVal(0.,0,conv_r_in[lev]->nComp(),conv_r_in[lev]->nGrow());

        if (!constant_density)
        {
            num_comp = 1; 

            incflo_compute_slopes(lev, time, *density_in[lev], xslopes_r, yslopes_r, zslopes_r, 0, num_comp);
 
            fx[lev].reset(new MultiFab(m_u_mac[lev]->boxArray(),dmap[lev],num_comp,flux_ngrow,MFInfo(),factory));
            fy[lev].reset(new MultiFab(m_v_mac[lev]->boxArray(),dmap[lev],num_comp,flux_ngrow,MFInfo(),factory));
            fz[lev].reset(new MultiFab(m_w_mac[lev]->boxArray(),dmap[lev],num_comp,flux_ngrow,MFInfo(),factory));

            // Note that the "ntrac" component of scal_forces holds zeroes
            incflo_compute_fluxes(lev, fx, fy, fz, density_in, 0, scal_forces_in, ntrac, num_comp,
                                  xslopes_r, yslopes_r, zslopes_r, 0,
                                  m_u_mac, m_v_mac, m_w_mac);

            incflo_divergence_plus_redist(lev, conv_r_in, fx, fy, fz, num_comp);

        }

        // **********************************************************
        // Compute div (rho trac u) -- the update for (rho*trac)
        // **********************************************************
        conv_t_in[lev]->setVal(0.,0,conv_t_in[lev]->nComp(),conv_t_in[lev]->nGrow());

        if (advect_tracer)
        {
            num_comp = ntrac; 

            incflo_compute_slopes(lev, time, *tracer_in[lev], xslopes_t, yslopes_t, zslopes_t, 0, num_comp);
 
            fx[lev].reset(new MultiFab(m_u_mac[lev]->boxArray(),dmap[lev],num_comp,flux_ngrow,MFInfo(),factory));
            fy[lev].reset(new MultiFab(m_v_mac[lev]->boxArray(),dmap[lev],num_comp,flux_ngrow,MFInfo(),factory));
            fz[lev].reset(new MultiFab(m_w_mac[lev]->boxArray(),dmap[lev],num_comp,flux_ngrow,MFInfo(),factory));

            incflo_compute_fluxes(lev, fx, fy, fz, tracer_in, 0, scal_forces_in, 0, num_comp,
                                  xslopes_t, yslopes_t, zslopes_t, 0,
                                  m_u_mac, m_v_mac, m_w_mac);

            incflo_divergence_plus_redist(lev, conv_t_in, fx, fy, fz, num_comp);
        }

        // Convert (rho * tracer) back to tracer
        if (advect_tracer)
        {
            for (int i = 0; i < ntrac; i++) 
               MultiFab::Divide(*tracer_in[lev],*density_in[lev],0,i,1,tracer_in[lev]->nGrow());
        }

        // Return the negative
        conv_u_in[lev] -> mult(-1.0);
        conv_r_in[lev] -> mult(-1.0);
        conv_t_in[lev] -> mult(-1.0);
    } // lev
}

void 
incflo::incflo_divergence_plus_redist(const int lev, 
                                      Vector< std::unique_ptr<MultiFab> >& conv_in,
                                      Vector< std::unique_ptr<MultiFab> >& fx,
                                      Vector< std::unique_ptr<MultiFab> >& fy,
                                      Vector< std::unique_ptr<MultiFab> >& fz,
                                      int num_comp)
{
    // Note conv_tmp needs two ghost cells for the redistribution step.
#ifdef AMREX_USE_EB
    MultiFab conv_tmp(grids[lev], dmap[lev], num_comp, 2, MFInfo(), *ebfactory[lev]);
#else
    MultiFab conv_tmp(grids[lev], dmap[lev], num_comp, 2, MFInfo());
#endif
    conv_tmp.setVal(0.);

    Array<MultiFab*,AMREX_SPACEDIM> fluxes;

    fluxes[0] = fx[lev].get();
    fluxes[1] = fy[lev].get();
    fluxes[2] = fz[lev].get();

#ifdef AMREX_USE_EB
    bool already_on_centroids = true;
    EB_computeDivergence(conv_tmp, GetArrOfConstPtrs(fluxes), geom[lev], already_on_centroids);
    amrex::single_level_redistribute( lev, conv_tmp, *conv_in[lev], 0, num_comp, geom);
#else
    computeDivergence(*conv_in[lev], GetArrOfConstPtrs(fluxes), geom[lev]);
#endif
}


