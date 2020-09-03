FROM neurodebian:nd18.04-non-free

# Command-line arguments for `./configure`
ARG MRTRIX3_CONFIGURE_FLAGS=""
# Command-line arguments for `./build`
ARG MRTRIX3_BUILD_FLAGS=""
# Temporary dependencies for other software packages
ARG OTHER_TEMP_DEPS="curl file wget"

# Prevent programs like `apt-get` from presenting interactive prompts.
ARG DEBIAN_FRONTEND="noninteractive"

ENV FSLDIR="/opt/fsl"
ENV PATH="/opt/mrtrix3/bin:/opt/fsl/bin:$PATH"

# Install MRtrix3 compile-time dependencies.
RUN apt-get -qq update \
    && apt-get install -yq --no-install-recommends \
          $OTHER_TEMP_DEPS \
          bc \
          ca-certificates \
          dc \
          g++-7 \
          git \
          libeigen3-dev \
          libfftw3-dev \
          libgl1-mesa-dev \
          libpng-dev \
          libqt5opengl5-dev \
          libqt5svg5-dev \
          libtiff5-dev \
          python \
          qt5-default \
          zlib1g-dev

# Install ANTs.
RUN apt-get -qq update \
    && apt-get install -yq --no-install-recommends "ants"

# Install FSL.
RUN wget -q http://fsl.fmrib.ox.ac.uk/fsldownloads/fslinstaller.py -O /fslinstaller.py \
    && chmod 775 /fslinstaller.py \
    && python2 /fslinstaller.py -d /opt/fsl -V 6.0.4 -q \
    && rm -f /fslinstaller.py \
    && ( which immv || ( rm -rf /opt/fsl/fslpython && /opt/fsl/etc/fslconf/fslpython_install.sh -f /opt/fsl || ( cat /tmp/fslpython*/fslpython_miniconda_installer.log && exit 1 ) ) )

# Do a system cleanup.
RUN apt-get clean \
    && apt-get remove --purge -y `apt-mark showauto` $OTHER_TEMP_DEPS \
    && rm -rf /var/lib/apt/lists/*

# Configure to be immediately ready to work on MRtrix3 
RUN mkdir /opt/mrtrix3
WORKDIR /opt/mrtrix3

# git commitish to be checked out provided as argument upon execution of container
ENTRYPOINT ["bash", "-c", "source /opt/fsl/etc/fslconf/fsl.sh \
                           && git clone https://github.com/MRtrix3/mrtrix3.git . \
                           && git checkout $1 \
                           && ./configure $MRTRIX3_CONFIGURE_FLAGS \
                           && ./build $MRTRIX3_BUILD_FLAGS \
                           && ( run_tests scripts || ( cat testing_scripts.log && exit 1 ) )]

