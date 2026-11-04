ARG BASE_IMAGE=mwnet-registration:alpha2-4a92d877b4
FROM ${BASE_IMAGE}
USER root
# git-bundle is a checksummed private QA input, copied from the exact Ubuntu
# builder used for the candidate. No downloads or package installation occur.
COPY git-bundle /usr/local/lib/mwnet-qa
COPY git /usr/local/bin/git
RUN chmod 0755 /usr/local/bin/git /usr/local/lib/mwnet-qa/git && git --version
ENV GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1
WORKDIR /tmp
USER 1000:1000
LABEL org.mwnet.scope="Private acceptance worker, not a beta product image"
