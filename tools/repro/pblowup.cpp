// What doubles the covariance at t = 362 s in the degenerate twin?
//
// Rather than logging every if/else, log WHICH BLOCK OF P MOVES FIRST. The state
// vector is partitioned, so the first block whose variance departs localises the
// mechanism without guessing which branch to instrument.
//
// Reads an exported sensor log, replays it through fusioncore_core, and prints the
// diagonal blocks of P over a time window.
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include "fusioncore/fusioncore.hpp"
#include "fusioncore/motion_model.hpp"
using namespace fusioncore;

static double yaw_sigma_deg(const State& s) {
  // 1-sigma of yaw from the quaternion covariance, via the small-angle Jacobian
  // d(yaw)/d(QZ) = 2 near identity. Reported as an actual measurement rather than
  // inferred from path ratio.
  const double qw = s.x[QW], qz = s.x[QZ];
  const double denom = qw*qw + qz*qz;
  if (denom < 1e-12) return -1.0;
  const double dyaw_dqz = 2.0 * qw / denom;
  const double dyaw_dqw = -2.0 * qz / denom;
  const double var = dyaw_dqz*dyaw_dqz*s.P(QZ,QZ)
                   + dyaw_dqw*dyaw_dqw*s.P(QW,QW)
                   + 2.0*dyaw_dqz*dyaw_dqw*s.P(QW,QZ);
  return var > 0 ? std::sqrt(var) * 180.0 / M_PI : 0.0;
}

int main(int argc, char** argv) {
  if (argc < 4) { std::fprintf(stderr, "usage: pblowup <log> <q_angular_vel> <t_lo> <t_hi>\n"); return 2; }
  const std::string path = argv[1];
  const double q   = std::atof(argv[2]);
  const double tlo = argc > 3 ? std::atof(argv[3]) : 340.0;
  const double thi = argc > 4 ? std::atof(argv[4]) : 380.0;

  FusionCoreConfig cfg;
  cfg.imu.gyro_noise_x=cfg.imu.gyro_noise_y=cfg.imu.gyro_noise_z=0.005;
  cfg.imu.accel_noise_x=cfg.imu.accel_noise_y=cfg.imu.accel_noise_z=0.1;
  cfg.imu_has_magnetometer=false;
  cfg.outlier_rejection=true;
  cfg.adaptive_imu=cfg.adaptive_encoder=cfg.adaptive_gnss=true;
  cfg.motion_model=create_motion_model("DifferentialDrive");
  cfg.ukf.tangent_space_attitude=true;
  cfg.ukf.tangent_space_quaternion_mean=true;
  cfg.ukf.q_orientation=1e-9;
  cfg.ukf.q_angular_vel=q;

  FusionCore fc(cfg);
  bool inited=false; double t0=-1;
  std::ifstream in(path);
  std::string line;
  std::printf("# q_angular_vel=%g window %.0f..%.0f s\n", q, tlo, thi);
  std::printf("# %9s %10s %11s %11s %11s %11s %11s %11s %9s\n",
              "t_rel","yaw_sig","P_pos","P_vel","P_quat","P_angvel","P_accel","P_gbias","zupt_std");
  double last_print=-1;
  while (std::getline(in, line)) {
    std::istringstream ss(line);
    std::string kind; ss >> kind;
    double t;
    if (kind=="IMU") {
      double wx,wy,wz,ax,ay,az; ss >> t >> wx >> wy >> wz >> ax >> ay >> az;
      if (t0<0) t0=t;
      if (!inited) { fc.init(State{}, t); inited=true; }
      fc.update_imu(t,wx,wy,wz,ax,ay,az);
    } else if (kind=="ENC") {
      double vx,vy,wz; ss >> t >> vx >> vy >> wz;
      if (!inited) continue;
      fc.update_encoder(t,vx,vy,wz);
    } else if (kind=="GNSS") {
      double e,n,u,hd,vd,sats,ft; ss >> t >> e >> n >> u >> hd >> vd >> sats >> ft;
      if (!inited) continue;
      sensors::GnssFix fix; fix.x=e; fix.y=n; fix.z=u;
      fix.hdop=hd; fix.vdop=vd; fix.sigma_xy=hd; fix.sigma_z=vd;
      fix.satellites=(int)sats;
      fix.fix_type=(sensors::GnssFixType)(int)ft;
      fc.update_gnss(t,fix);
    } else continue;
    if (t0<0) continue;
    const double tr = t - t0;
    if (tr < tlo || tr > thi) { if (tr > thi) break; continue; }
    if (tr - last_print < 0.5) continue;
    last_print = tr;
    const State& s = fc.get_state();
    auto blk=[&](int a,int b){ double v=0; for(int i=a;i<=b;++i) v+=s.P(i,i); return v; };
    FusionCoreStatus st = fc.get_status();
    std::printf("  %9.2f %10.4f %11.6f %11.6f %11.3e %11.6f %11.6f %11.3e %9.4f\n",
                tr, yaw_sigma_deg(s), blk(X,Z), blk(VX,VZ), blk(QW,QZ),
                blk(WX,WZ), blk(AX,AZ), blk(B_GX,B_GZ), st.zupt_accel_std);
  }
  return 0;
}
