#include <QString>
#include <QImage>
#include "reg.hpp"
#include "tract_model.hpp"



int after_warp(const std::vector<std::string>& apply_warp_filename,dual_reg<3>& r)
{
    for(const auto& each_file: apply_warp_filename)
    {
        if(tipl::ends_with(each_file,".tt.gz"))
        {
            if(!r.apply_warping_tt(each_file.c_str(),(each_file+".wp.tt.gz").c_str()))
                tipl::error() << r.error_msg;
        }
        else
        {
            if(!r.apply_warping(each_file.c_str(),(each_file+".wp.nii.gz").c_str()))
                tipl::error() << r.error_msg;
        }
    }
    return 0;
}


bool load_nifti_file(std::string file_name_cmd,
                     tipl::image<3>& data,
                     tipl::vector<3>& vs,
                     tipl::matrix<4,4>& trans,
                     bool& is_mni)
{
    std::istringstream in(file_name_cmd);
    std::string file_name,cmd;
    std::getline(in,file_name,'+');
    if(!tipl::io::gz_nifti::load_from_file(file_name.c_str(),data,vs,trans,is_mni))
    {
        tipl::error() << "cannot load file " << file_name << std::endl;
        return false;
    }
    while(std::getline(in,cmd,'+'))
    {
        tipl::out() << "apply " << cmd << std::endl;
        if(cmd == "gaussian")
            tipl::filter::gaussian(data);
        else
        if(cmd == "sobel")
            tipl::filter::sobel(data);
        else
        if(cmd == "mean")
            tipl::filter::mean(data);
        else
        {
            tipl::error() << "unknown command " << cmd << std::endl;
            return false;
        }
    }
    return true;
}
bool load_nifti_file(std::string file_name_cmd,tipl::image<3>& data,tipl::vector<3>& vs)
{
    tipl::matrix<4,4> trans;
    bool is_mni;
    return load_nifti_file(file_name_cmd,data,vs,trans,is_mni);
}


int reg(tipl::program_option<tipl::out>& po)
{

    std::vector<std::string> apply_warp_filename;
    if(po.has("apply_warp"))
    {
        if(!po.get_files("apply_warp",apply_warp_filename))
        {
            tipl::error() << "cannot find file " << po.get("apply_warp") <<std::endl;
            return 1;
        }
        if(!po.get("overwrite",0))
        {
            bool skip = true;
            for(const auto& each_file: apply_warp_filename)
            {
                if((tipl::ends_with(each_file,".tt.gz") && !std::filesystem::exists(each_file+".wp.tt.gz")) ||
                   (tipl::ends_with(each_file,".nii.gz") && !std::filesystem::exists(each_file+".wp.nii.gz")))
                {
                    skip = false;
                    break;
                }
            }
            if(skip)
            {
                tipl::out() << "output file exists, skipping";
                return 0;
            }
        }
    }

    dual_reg<3> r;

    if(po.has("warp"))
    {
        if(!po.has("apply_warp"))
        {
            tipl::error() << "please specify the images or tracts to be warped using --apply_warp";
            return 1;
        }
        tipl::out() << "loading warping field";
        if(!r.load_warping(po.get("warp")))
            goto error;
        return after_warp(apply_warp_filename,r);
    }

    if(!po.has("from") || !po.has("to"))
    {
        tipl::error() << "please specify the images to normalize using --from and --to";
        return 1;
    }

    if(!r.load_subject(0,po.get("from")) ||
       !r.load_template(0,po.get("to")))
        goto error;
    r.modality_names[0] = std::filesystem::path(po.get("from")).stem().stem().string() + "->" +
                          std::filesystem::path(po.get("to")).stem().stem().string();

    if(po.has("from2") && po.has("to2"))
    {
        if(!r.load_subject(1,po.get("from2")) ||
           !r.load_template(1,po.get("to2")))
            goto error;
        r.modality_names[1] = std::filesystem::path(po.get("from2")).stem().stem().string() + "->" +
                              std::filesystem::path(po.get("to2")).stem().stem().string();
    }

    tipl::out() << "from dim: " << r.Is;
    tipl::out() << "to dim: " << r.Its;

    tipl::out() << "running linear registration." << std::endl;

    if(po.get("large_deform",0))
        r.bound = tipl::reg::large_bound;
    r.reg_type = po.get("reg_type",1) == 0 ? tipl::reg::rigid_body : tipl::reg::affine;
    r.cost_type = po.get("cost_function",r.reg_type==tipl::reg::rigid_body ? "mi" : "corr") == std::string("mi") ? tipl::reg::mutual_info : tipl::reg::corr;
    r.skip_linear = po.get("skip_linear",0);
    r.skip_nonlinear = po.get("skip_nonlinear",0);

    r.linear_reg(tipl::prog_aborted);

    if(r.reg_type != tipl::reg::rigid_body)
    {
        r.param.resolution = po.get("resolution",r.param.resolution);
        r.param.speed = po.get("speed",r.param.speed);
        r.param.smoothing = po.get("smoothing",r.param.smoothing);
        r.param.min_dimension = po.get("min_dimension",r.param.min_dimension);
        r.nonlinear_reg(tipl::prog_aborted);
    }

    if(po.has("output_warp") && !r.save_warping(po.get("output_warp").c_str()))
        goto error;
    return after_warp(apply_warp_filename,r);

    error:
    tipl::error() << r.error_msg;
    return 1;
}
